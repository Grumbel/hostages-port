// AmigaOS high-level emulation: exec, trackdisk.device and the boot strap,
// as far as the game (bootblock, stage-2 loader and the two programs) uses
// them. Everything else of the game talks to the hardware directly.
#include "amiga/hle.hpp"
#include "amiga/hunk.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace amiga {

namespace {
// Exec structure offsets
constexpr uint32_t LN_SUCC = 0, LN_PRED = 4, LN_TYPE = 8, LN_PRI = 9, LN_NAME = 10;
constexpr uint32_t LH_HEAD = 0, LH_TAIL = 4, LH_TAILPRED = 8;
constexpr uint32_t MP_FLAGS = 14, MP_SIGBIT = 15, MP_SIGTASK = 16, MP_MSGLIST = 20;
constexpr uint32_t MN_REPLYPORT = 14;
constexpr uint32_t IO_DEVICE = 20, IO_UNIT = 24, IO_COMMAND = 28, IO_FLAGS = 30, IO_ERROR = 31;
constexpr uint32_t IO_ACTUAL = 32, IO_LENGTH = 36, IO_DATA = 40, IO_OFFSET = 44;
constexpr uint32_t IS_DATA = 14, IS_CODE = 18;
constexpr uint32_t TC_SIGALLOC = 18, TC_SIGWAIT = 22, TC_SIGRECVD = 26, TC_TRAPALLOC = 34;
constexpr uint32_t TC_SPLOWER = 58, TC_SPUPPER = 62;
constexpr uint8_t NT_TASK = 1, NT_DEVICE = 3, NT_MSGPORT = 4, NT_MESSAGE = 5, NT_REPLYMSG = 7,
                  NT_LIBRARY = 9, NT_PROCESS = 13;
constexpr uint8_t IOF_QUICK = 1;
constexpr int8_t IOERR_OPENFAIL = -1, IOERR_NOCMD = -3, TDERR_SEEKERROR = 29;
constexpr uint32_t MEMF_CHIP = 2, MEMF_FAST = 4, MEMF_CLEAR = 0x10000, MEMF_LARGEST = 0x20000;

// Exec base offsets
constexpr uint32_t EB_THISTASK = 276, EB_IDNEST = 294, EB_TDNEST = 295, EB_ATTNFLAGS = 296;
constexpr uint32_t EB_MEMLIST = 322, EB_RESLIST = 336, EB_DEVLIST = 350, EB_INTRLIST = 364;
constexpr uint32_t EB_LIBLIST = 378, EB_PORTLIST = 392, EB_TASKREADY = 406, EB_TASKWAIT = 420;
constexpr uint32_t EB_VBLANKFREQ = 530, EB_POWERFREQ = 531;

// Intuition/graphics structure offsets (struct Screen, ViewPort)
constexpr uint32_t SC_WIDTH = 12, SC_HEIGHT = 14, SC_VIEWPORT = 44, SC_RASTPORT = 84,
                   SC_BITMAP = 184, SC_SIZE = 346;
constexpr uint32_t VP_COLORMAP = 4, VP_DWIDTH = 24, VP_DHEIGHT = 26, VP_DYOFFSET = 30,
                   VP_MODES = 32, VP_RASINFO = 36;
constexpr uint16_t V_HIRES = 0x8000, V_HAM = 0x0800, V_DUALPF = 0x0400;

// Memory map: vectors and exec structures in low chip RAM; like Kickstart
// on an A500 with a trapdoor expansion, the stacks live in slow RAM so the
// game gets as much chip RAM as possible (the assault part needs ~470 KB).
constexpr uint32_t LOW_END = 0x2000;
constexpr uint32_t SSP_TOP = SLOW_BASE + 0x4000;
constexpr uint32_t USP_LOW = SSP_TOP;
constexpr uint32_t USP_TOP = SSP_TOP + 0x8000;

const char* lib_name(int lib) {
  static const char* names[] = {"exec", "dos", "graphics", "trackdisk", "intuition", "mathffp"};
  return lib < LIB_COUNT ? names[lib] : "?";
}

uint16_t host_trap(int idx) { return uint16_t(0xA000 | (HOST_LIB << 8) | idx); }
}  // namespace

// ---- setup ------------------------------------------------------------------

uint32_t Hle::low_alloc(uint32_t size) {
  uint32_t a = (low_next_ + 3) & ~3u;
  low_next_ = a + size;
  if (low_next_ > LOW_END) m.log("HLE: low memory exhausted\n");
  for (uint32_t i = 0; i < size; ++i) m.w8(a + i, 0);
  return a;
}

uint32_t Hle::make_string(const char* s) {
  uint32_t a = low_alloc(uint32_t(std::strlen(s)) + 1);
  for (size_t i = 0; s[i]; ++i) m.w8(a + uint32_t(i), uint8_t(s[i]));
  return a;
}

uint32_t Hle::make_library(int lib, const char* name, int nvec, uint32_t possize, uint8_t type) {
  uint32_t neg = uint32_t(nvec) * 6;
  uint32_t block = low_alloc(neg + possize);
  uint32_t base = block + neg;
  for (int i = 1; i <= nvec; ++i) {
    uint32_t v = base - uint32_t(i) * 6;
    m.w16(v, uint16_t(0xA000 | (lib << 8) | i));
    m.w16(v + 2, 0x4E75);  // rts
    m.w16(v + 4, 0x4E71);
  }
  uint32_t name_addr = make_string(name);
  m.w8(base + LN_TYPE, type);
  m.w32(base + LN_NAME, name_addr);
  m.w16(base + 16, uint16_t(neg));
  m.w16(base + 18, uint16_t(possize));
  m.w16(base + 20, 34);  // version (Kickstart 1.3)
  m.w16(base + 22, 5);
  m.w32(base + 24, name_addr);
  libs_[lib] = base;
  return base;
}

void Hle::boot() {
  ret_stub = low_alloc(4);
  m.w16(ret_stub, host_trap(TRAP_RETURN));
  uint32_t boot_ret = low_alloc(4);
  m.w16(boot_ret, host_trap(TRAP_BOOT_DONE));
  // Autovector interrupt handlers: host trap (exec's dispatcher) + RTE.
  for (int level = 1; level <= 6; ++level) {
    uint32_t stub = low_alloc(4);
    m.w16(stub, host_trap(TRAP_IRQ + level - 1));
    m.w16(stub + 2, 0x4E73);
    m.w32(uint32_t(24 + level) * 4, stub);
  }

  uint32_t exec = make_library(LIB_EXEC, "exec.library", 130, 632, NT_LIBRARY);
  m.w32(4, exec);
  m.w16(exec + 20, 34);
  m.w8(exec + EB_IDNEST, 0xFF);
  m.w8(exec + EB_TDNEST, 0xFF);
  m.w16(exec + EB_ATTNFLAGS, 0);  // plain 68000
  m.w32(exec + 62, CHIP_SIZE);    // MaxLocMem
  m.w32(exec + 78, SLOW_BASE + SLOW_SIZE);  // MaxExtMem
  m.w8(exec + EB_VBLANKFREQ, 50);
  m.w8(exec + EB_POWERFREQ, 50);
  for (uint32_t l : {EB_MEMLIST, EB_RESLIST, EB_DEVLIST, EB_INTRLIST, EB_LIBLIST, EB_PORTLIST,
                     EB_TASKREADY, EB_TASKWAIT})
    new_list(exec + l);
  lib_list_ = exec + EB_LIBLIST;
  port_list_ = exec + EB_PORTLIST;
  add_tail(lib_list_, exec);

  add_tail(lib_list_, make_library(LIB_DOS, "dos.library", 40, 64, NT_LIBRARY));
  uint32_t gfx = make_library(LIB_GFX, "graphics.library", 110, 0x200, NT_LIBRARY);
  add_tail(lib_list_, gfx);
  // GfxBase->copinit, the system copper list: like the real one it ends
  // by jumping (COP2LC/COPJMP2) to the current View's list, which
  // show_front_screen() sets; programs restore the display by writing it
  // to COP1LC. Until a screen is open the View's list is empty.
  uint32_t empty = low_alloc(8);
  m.w32(empty, 0x01000200);   // BPLCON0 = colour, no planes
  m.w32(empty + 4, 0xFFFFFFFE);
  copinit_ = low_alloc(24);
  m.w32(copinit_, 0x01000200);
  m.w16(copinit_ + 4, 0x0084);
  m.w16(copinit_ + 6, uint16_t(empty >> 16));
  m.w16(copinit_ + 8, 0x0086);
  m.w16(copinit_ + 10, uint16_t(empty));
  m.w32(copinit_ + 12, 0x008A0000);  // COPJMP2
  m.w32(copinit_ + 16, 0xFFFFFFFE);
  m.w32(gfx + 38, copinit_);  // copinit
  m.w32(gfx + 50, empty);     // LOFlist
  m.w16(gfx + 206, 4);      // DisplayFlags: PAL
  m.w16(gfx + 212, 311);    // MaxDisplayRow
  m.w16(gfx + 216, 256);    // NormalDisplayRows
  m.w16(gfx + 218, 640);    // NormalDisplayColumns
  add_tail(lib_list_, make_library(LIB_INTUITION, "intuition.library", 120, 0x60, NT_LIBRARY));
  add_tail(lib_list_, make_library(LIB_MATHFFP, "mathffp.library", 20, 64, NT_LIBRARY));
  uint32_t td = make_library(LIB_TRACKDISK, "trackdisk.device", 8, 64, NT_DEVICE);
  add_tail(exec + EB_DEVLIST, td);
  td_unit_ = low_alloc(64);

  // The boot task (strap runs in the context of the initial task).
  task_ = low_alloc(228);
  m.w8(task_ + LN_TYPE, NT_PROCESS);
  m.w32(task_ + LN_NAME, make_string("initialCLI"));
  m.w8(task_ + 15, 2);  // TS_RUN
  m.w32(task_ + TC_SIGALLOC, sig_alloc_);
  m.w16(task_ + TC_TRAPALLOC, 0x8000);
  m.w32(task_ + TC_SPLOWER, USP_LOW);
  m.w32(task_ + TC_SPUPPER, USP_TOP);
  uint32_t port = task_ + 92;  // pr_MsgPort
  m.w8(port + LN_TYPE, NT_MSGPORT);
  m.w8(port + MP_SIGBIT, 8);
  m.w32(port + MP_SIGTASK, task_);
  new_list(port + MP_MSGLIST);
  m.w32(exec + EB_THISTASK, task_);

  // The IORequest strap hands to the bootblock: trackdisk unit 0 is open.
  uint32_t io = low_alloc(56);
  m.w8(io + LN_TYPE, NT_REPLYMSG);
  m.w32(io + MN_REPLYPORT, port);
  m.w16(io + 18, 56);
  m.w32(io + IO_DEVICE, td);
  m.w32(io + IO_UNIT, td_unit_);

  // Free memory: chip RAM above the stacks and the slow RAM expansion.
  heap_start = LOW_END;
  m.free_list_.clear();
  m.add_free(heap_start, CHIP_SIZE - heap_start);
  m.add_free(USP_TOP, SLOW_BASE + SLOW_SIZE - USP_TOP);
  // exec's MemList: one MemHeader per memory region.
  static const struct { const char* name; uint16_t attr; uint32_t lower, upper; int8_t pri; } regions[] = {
      {"chip memory", 0x0003, LOW_END, CHIP_SIZE, -10},             // MEMF_CHIP|MEMF_PUBLIC
      {"expansion memory", 0x0005, USP_TOP, SLOW_BASE + SLOW_SIZE, -5}};  // MEMF_FAST|MEMF_PUBLIC
  for (int i = 0; i < 2; ++i) {
    uint32_t mh = low_alloc(32);
    m.w8(mh + LN_TYPE, 10);  // NT_MEMORY
    m.w8(mh + LN_PRI, uint8_t(regions[i].pri));
    m.w32(mh + LN_NAME, make_string(regions[i].name));
    m.w16(mh + 14, regions[i].attr);
    m.w32(mh + 20, regions[i].lower);
    m.w32(mh + 24, regions[i].upper);
    enqueue(exec + EB_MEMLIST, mh);
    mem_headers_[i] = mh;
  }
  update_memlist();

  // CIA set-up done by Kickstart: keyboard serial interrupt enabled.
  m.cia_[0].icr_mask = Cia::ICR_SP;
  m.cs_.intena_ = INT_INTEN | INT_PORTS | INT_VERTB | INT_EXTER | INT_SOFT;

  if (m.disk.is_dos()) {
    boot_dos();
    return;
  }

  // strap: read the bootblock into chip RAM (checksum and "DOS" tag are
  // valid on the original disk).
  uint32_t bootblock = m.alloc(1024, true);
  uint8_t bb[1024];
  m.disk.read(0, sizeof bb, bb);
  for (uint32_t i = 0; i < sizeof bb; ++i) m.w8(bootblock + i, bb[i]);

  // CPU: strap runs in user mode on the initial task's stack.
  auto& c = m.cpu;
  c.s = false;
  c.osp = SSP_TOP;
  c.a[7] = USP_TOP;
  c.imask = 0;
  c.push32(boot_ret);
  c.a[1] = io;
  c.a[6] = exec;
  c.pc = bootblock + 12;
}

void Hle::boot_done() {
  // The bootblock returns d0 = 0 and the address to continue at in a0
  // (normally dos.library's init code; here: the stage-2 loader).
  if (D(0) != 0) {
    m.log("bootblock failed (d0=%d)\n", int(D(0)));
    throw QuitRequested{};
  }
  if (m.verbose) m.log("[boot] bootblock done, entering %06x\n", A(0));
  // Hook stage 2's "jsr (a6)" that starts a loaded program (offset $256 of
  // its run_program(name) routine) so the program can be identified,
  // patched and registered before it runs.
  stage2_ = A(0);
  if (m.r16(stage2_ + 0x256) == 0x4E96)
    m.w16(stage2_ + 0x256, host_trap(TRAP_RUN_PROGRAM));
  else
    m.log("unknown stage-2 loader; programs will not be patched\n");
  // Debugging: start with the second program (stage 2 loads "apro1." and
  // "it." alternately; the name strings are at $27E and $285).
  if (getenv("HOSTAGES_START_IT") && m.cstr(stage2_ + 0x27E) == "apro1.")
    for (uint32_t i = 0; i < 6; ++i) m.w8(stage2_ + 0x27E + i, uint8_t("it.\0\0\0"[i]));
  m.cpu.push32(ret_stub);
  m.cpu.a[6] = libs_[LIB_EXEC];
  m.cpu.pc = A(0);
}

void Hle::run_program() {
  // In stage 2's run_program(char* name): a5 = its frame, a4 = its small
  // data base; the hunk table at -32766(a4) holds {address, size} × 3.
  std::string name = m.cstr(m.r32(A(5) + 8), 16);
  std::vector<uint32_t> addrs, sizes;
  for (uint32_t i = 0; i < 3; ++i) {
    uint32_t e = A(4) - 32766 + i * 8;
    addrs.push_back(m.r32(e));
    sizes.push_back(m.r32(e + 4));
  }
  m.program_loaded(name, addrs, sizes);
  // jsr (a6)
  m.cpu.push32(m.cpu.pc);
  m.cpu.pc = A(6);
}

// ---- dispatch ---------------------------------------------------------------

void Hle::unknown(int lib, int lvo) {
  m.log("HLE: unimplemented %s LVO -%d (called from %06x)\n", lib_name(lib), lvo,
        m.r32(m.cpu.a[7]));
  D(0) = 0;
}

void Hle::call(int lib, int lvo) {
  if (m.verbose && !(lib == LIB_EXEC && (lvo == 120 || lvo == 126)))
    m.log("[hle] %s -%d from %06x d0=%08x d1=%08x a0=%08x a1=%08x\n", lib_name(lib), lvo,
          m.r32(m.cpu.a[7]), D(0), D(1), A(0), A(1));
  switch (lib) {
    case LIB_EXEC: exec(lvo); break;
    case LIB_DOS: dos(lvo); break;
    case LIB_GFX: gfx(lvo); break;
    case LIB_TRACKDISK: trackdisk(lvo); break;
    case LIB_INTUITION: intuition(lvo); break;
    case LIB_MATHFFP: mathffp(lvo); break;
    default: unknown(lib, lvo); break;
  }
}

// ---- exec lists / messages ------------------------------------------------

void Hle::new_list(uint32_t l) {
  m.w32(l + LH_HEAD, l + LH_TAIL);
  m.w32(l + LH_TAIL, 0);
  m.w32(l + LH_TAILPRED, l + LH_HEAD);
}

void Hle::add_tail(uint32_t l, uint32_t n) {
  uint32_t tp = m.r32(l + LH_TAILPRED);
  m.w32(n + LN_SUCC, l + LH_TAIL);
  m.w32(n + LN_PRED, tp);
  m.w32(tp + LN_SUCC, n);
  m.w32(l + LH_TAILPRED, n);
}

void Hle::add_head(uint32_t l, uint32_t n) {
  uint32_t h = m.r32(l + LH_HEAD);
  m.w32(n + LN_SUCC, h);
  m.w32(n + LN_PRED, l + LH_HEAD);
  m.w32(h + LN_PRED, n);
  m.w32(l + LH_HEAD, n);
}

uint32_t Hle::rem_head(uint32_t l) {
  uint32_t h = m.r32(l + LH_HEAD);
  uint32_t next = m.r32(h + LN_SUCC);
  if (!next) return 0;
  m.w32(l + LH_HEAD, next);
  m.w32(next + LN_PRED, l + LH_HEAD);
  return h;
}

uint32_t Hle::rem_tail(uint32_t l) {
  uint32_t t = m.r32(l + LH_TAILPRED);
  uint32_t pred = m.r32(t + LN_PRED);
  if (!pred) return 0;
  m.w32(l + LH_TAILPRED, pred);
  m.w32(pred + LN_SUCC, l + LH_TAIL);
  return t;
}

void Hle::remove(uint32_t n) {
  uint32_t succ = m.r32(n + LN_SUCC), pred = m.r32(n + LN_PRED);
  if (!succ || !pred) return;
  m.w32(pred + LN_SUCC, succ);
  m.w32(succ + LN_PRED, pred);
}

void Hle::insert(uint32_t l, uint32_t n, uint32_t pred) {
  if (!pred) { add_head(l, n); return; }
  uint32_t succ = m.r32(pred + LN_SUCC);
  if (!succ) { add_tail(l, n); return; }  // pred is the tail node: append
  m.w32(n + LN_SUCC, succ);
  m.w32(n + LN_PRED, pred);
  m.w32(succ + LN_PRED, n);
  m.w32(pred + LN_SUCC, n);
}

void Hle::enqueue(uint32_t l, uint32_t n) {
  int8_t pri = int8_t(m.r8(n + LN_PRI));
  uint32_t cur = m.r32(l + LH_HEAD);
  while (m.r32(cur + LN_SUCC)) {
    if (int8_t(m.r8(cur + LN_PRI)) < pri) break;
    cur = m.r32(cur + LN_SUCC);
  }
  uint32_t pred = m.r32(cur + LN_PRED);
  m.w32(n + LN_SUCC, cur);
  m.w32(n + LN_PRED, pred);
  m.w32(pred + LN_SUCC, n);
  m.w32(cur + LN_PRED, n);
}

uint32_t Hle::find_name(uint32_t l, const std::string& name) {
  for (uint32_t n = m.r32(l + LH_HEAD); m.r32(n + LN_SUCC); n = m.r32(n + LN_SUCC)) {
    uint32_t np = m.r32(n + LN_NAME);
    if (np && m.cstr(np) == name) return n;
  }
  return 0;
}

void Hle::put_msg(uint32_t port, uint32_t msg) {
  m.w8(msg + LN_TYPE, NT_MESSAGE);
  add_tail(port + MP_MSGLIST, msg);
  if ((m.r8(port + MP_FLAGS) & 3) == 0) {  // PA_SIGNAL
    uint32_t task = m.r32(port + MP_SIGTASK);
    if (task == task_ || task == 0) sig_recvd_ |= 1u << m.r8(port + MP_SIGBIT);
  }
}

void Hle::reply_msg(uint32_t msg) {
  uint32_t port = m.r32(msg + MN_REPLYPORT);
  if (!port) { m.w8(msg + LN_TYPE, NT_REPLYMSG); return; }
  put_msg(port, msg);
  m.w8(msg + LN_TYPE, NT_REPLYMSG);
}

// Wait for signals. There is only one task, so a wait that is not yet
// satisfied re-executes the library call (the line-A trap) after the rest
// of the scanline has passed; interrupts that may send the signal are taken
// in between. Returns 0 if the call must be retried.
uint32_t Hle::wait(uint32_t sigs) {
  uint32_t got = sig_recvd_ & sigs;
  if (got) {
    sig_recvd_ &= ~got;
    return got;
  }
  if (!sigs) return 0;
  m.cpu.pc = m.cpu.op_pc;
  m.cpu.cycles = std::max(m.cpu.cycles, m.line_start_ + m.line_cycles_);
  return 0;
}

// ---- interrupts --------------------------------------------------------------

void Hle::run_servers(int bit) {
  // exec clears the request before running a server chain.
  m.cs_.intreq_ &= uint16_t(~(1 << bit));
  uint32_t d[8] = {}, a[8] = {};
  a[0] = 0xDFF000;
  a[6] = libs_[LIB_EXEC];
  // The list may change while servers run (RemIntServer from a server).
  std::vector<uint32_t> chain = servers_[bit];
  for (uint32_t is : chain) {
    a[1] = m.r32(is + IS_DATA);
    a[5] = m.r32(is + IS_CODE);
    m.call68k(a[5], d, a);
    if (!m.last_call_z()) break;
  }
}

void Hle::update_memlist() {
  for (uint32_t mh : mem_headers_) {
    if (!mh) continue;
    uint32_t lower = m.r32(mh + 20), upper = m.r32(mh + 24);
    uint32_t prev = mh + 16, total = 0;  // mh_First
    for (auto& [a, size] : m.free_list_) {
      if (a < lower || a >= upper) continue;
      m.w32(prev, a);       // link from the previous chunk (or mh_First)
      m.w32(a + 4, size);   // mc_Bytes
      total += size;
      prev = a;             // mc_Next is at offset 0
    }
    m.w32(prev, 0);
    m.w32(mh + 28, total);  // mh_Free
  }
}

void Hle::keyboard_irq() {
  // ciaa.resource + keyboard.device: acknowledge the CIA interrupt and
  // handshake the keyboard (the byte stays readable in SDR, which is where
  // the game polls it once per frame).
  m.cia_[0].read(0xD);
}

void Hle::irq(int level) {
  static const int lvl[14] = {1, 1, 1, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6};
  uint16_t pending = m.cs_.intena_ & m.cs_.intreq_;
  for (int bit = 13; bit >= 0; --bit) {
    if (lvl[bit] != level || !(pending & (1 << bit))) continue;
    if (vectors_[bit]) {
      uint32_t is = vectors_[bit];
      uint32_t d[8] = {}, a[8] = {};
      d[1] = pending;
      a[0] = 0xDFF000;
      a[1] = m.r32(is + IS_DATA);
      a[5] = m.r32(is + IS_CODE);
      a[6] = libs_[LIB_EXEC];
      m.call68k(a[5], d, a);  // the handler clears INTREQ itself
      continue;
    }
    switch (bit) {
      case 3:  // PORTS: ciaa.resource first, then the server chain
        keyboard_irq();
        run_servers(bit);
        break;
      case 4: case 5: case 13:  // COPER, VERTB, EXTER server chains
        run_servers(bit);
        break;
      default:  // no handler installed: acknowledge
        m.cs_.intreq_ &= uint16_t(~(1 << bit));
        break;
    }
  }
}

// ---- exec ---------------------------------------------------------------------

void Hle::exec(int lvo) {
  auto& c = m.cpu;
  uint32_t eb = libs_[LIB_EXEC];
  switch (lvo) {
    case 30: {  // Supervisor(a5 userFunc): run with S set, returns with RTE
      uint32_t ret = c.pop32();
      uint16_t old = c.sr();
      c.set_sr(uint16_t(old | 0x2000));
      c.push32(ret);
      c.push16(old);
      c.pc = A(5);
      return;
    }
    case 120:  // Disable
      if (++id_nest_ == 0) m.custom_write(0x09A, INT_INTEN);
      m.w8(eb + EB_IDNEST, uint8_t(id_nest_));
      return;
    case 126:  // Enable
      if (--id_nest_ < 0) { id_nest_ = -1; m.custom_write(0x09A, 0x8000 | INT_INTEN); }
      m.w8(eb + EB_IDNEST, uint8_t(id_nest_));
      return;
    case 132: case 138: return;  // Forbid / Permit: single task
    case 144: {  // SetSR(d0 newSR, d1 mask): only CCR and interrupt mask
      uint16_t old = c.sr();
      uint16_t mask = uint16_t(D(1)) & 0x071F;
      uint16_t v = uint16_t((old & ~mask) | (D(0) & mask));
      c.set_ccr(v & 0x1F);
      c.imask = uint8_t((v >> 8) & 7);
      D(0) = old;
      return;
    }
    case 150: {  // SuperState: supervisor mode on the user stack
      if (c.s) { D(0) = 0; return; }
      uint32_t usp = c.a[7];
      D(0) = c.osp;
      c.s = true;
      c.osp = usp;
      c.a[7] = usp;
      return;
    }
    case 156: {  // UserState(d0 sysStack)
      if (!c.s) return;
      uint32_t sp = c.a[7];
      c.s = false;
      c.a[7] = sp;
      c.osp = D(0);
      return;
    }
    case 162: {  // SetIntVector(d0 num, a1 is) → old
      int n = int(D(0) & 15);
      D(0) = vectors_[n];
      vectors_[n] = A(1);
      return;
    }
    case 168: {  // AddIntServer(d0 num, a1 is)
      int n = int(D(0) & 15);
      auto& v = servers_[n];
      int8_t pri = int8_t(m.r8(A(1) + LN_PRI));
      auto it = v.begin();
      while (it != v.end() && int8_t(m.r8(*it + LN_PRI)) >= pri) ++it;
      v.insert(it, A(1));
      m.custom_write(0x09A, uint16_t(0x8000 | (1 << n)));
      return;
    }
    case 174: {  // RemIntServer(d0 num, a1 is)
      auto& v = servers_[D(0) & 15];
      v.erase(std::remove(v.begin(), v.end(), A(1)), v.end());
      return;
    }
    case 180: {  // Cause(a1 is): run the soft interrupt now
      uint32_t is = A(1);
      uint32_t d[8] = {}, a[8] = {};
      a[1] = m.r32(is + IS_DATA);
      a[5] = m.r32(is + IS_CODE);
      a[6] = eb;
      m.call68k(a[5], d, a);
      return;
    }
    case 198: {  // AllocMem(d0 size, d1 requirements)
      uint32_t size = D(0), req = D(1);
      uint32_t r = 0;
      if (size && (req & MEMF_FAST)) r = m.alloc_in(size, SLOW_BASE, SLOW_BASE + SLOW_SIZE, req & MEMF_CLEAR);
      else if (size) r = m.alloc(size, req & MEMF_CHIP, req & MEMF_CLEAR);
      if (!r) m.log("AllocMem(%u, %x) failed\n", size, req);
      if (m.verbose) m.log("[hle] AllocMem(%u, %x) = %06x\n", size, req, r);
      D(0) = r;
      return;
    }
    case 204: {  // AllocAbs(d0 size, a1 location)
      uint32_t size = (D(0) + 7) & ~7u, loc = A(1) & ~7u;
      D(0) = 0;
      for (auto& [a, s] : m.free_list_) {
        if (a <= loc && loc + size <= a + s) {
          uint32_t blk = a, bs = s;
          m.free_list_.erase(blk);
          if (loc > blk) m.free_list_[blk] = loc - blk;
          if (loc + size < blk + bs) m.free_list_[loc + size] = blk + bs - loc - size;
          D(0) = loc;
          break;
        }
      }
      return;
    }
    case 210:  // FreeMem(a1 addr, d0 size)
      if (m.verbose) m.log("[hle] FreeMem(%06x, %u)\n", A(1), D(0));
      m.free(A(1), D(0));
      return;
    case 216: {  // AvailMem(d1 requirements)
      uint32_t req = D(1);
      D(0) = m.avail(req & MEMF_CHIP, req & MEMF_LARGEST, req & MEMF_FAST);
      return;
    }
    case 234: insert(A(0), A(1), A(2)); return;
    case 240: add_head(A(0), A(1)); return;
    case 246: add_tail(A(0), A(1)); return;
    case 252: remove(A(1)); return;
    case 258: D(0) = rem_head(A(0)); return;
    case 264: D(0) = rem_tail(A(0)); return;
    case 270: enqueue(A(0), A(1)); return;
    case 276: D(0) = find_name(A(0), m.cstr(A(1))); return;
    case 294: {  // FindTask(a1 name)
      if (!A(1)) { D(0) = task_; return; }
      std::string n = m.cstr(A(1));
      D(0) = n == m.cstr(m.r32(task_ + LN_NAME)) ? task_ : 0;
      return;
    }
    case 300: D(0) = 0; return;  // SetTaskPri
    case 306: {  // SetSignal(d0 new, d1 mask) → old
      uint32_t old = sig_recvd_;
      sig_recvd_ = (sig_recvd_ & ~D(1)) | (D(0) & D(1));
      m.w32(task_ + TC_SIGRECVD, sig_recvd_);
      D(0) = old;
      return;
    }
    case 312: D(0) = 0; return;  // SetExcept
    case 318: {  // Wait(d0 signals)
      uint32_t sigs = D(0);
      uint32_t got = wait(sigs);
      if (got) D(0) = got;
      return;
    }
    case 324:  // Signal(a1 task, d0 signals)
      if (A(1) == task_) sig_recvd_ |= D(0);
      return;
    case 330: {  // AllocSignal(d0 num)
      int want = int8_t(D(0));
      int got = -1;
      if (want >= 0) {
        if (!(sig_alloc_ & (1u << want))) got = want;
      } else {
        for (int i = 31; i >= 16; --i)
          if (!(sig_alloc_ & (1u << i))) { got = i; break; }
      }
      if (got >= 0) {
        sig_alloc_ |= 1u << got;
        sig_recvd_ &= ~(1u << got);
        m.w32(task_ + TC_SIGALLOC, sig_alloc_);
      }
      D(0) = uint32_t(got);
      return;
    }
    case 336:  // FreeSignal(d0 num)
      if (int32_t(D(0)) >= 0) sig_alloc_ &= ~(1u << (D(0) & 31));
      m.w32(task_ + TC_SIGALLOC, sig_alloc_);
      return;
    case 342: {  // AllocTrap(d0 num)
      int want = int8_t(D(0));
      int got = -1;
      if (want >= 0) {
        if (!(trap_alloc_ & (1u << want))) got = want;
      } else {
        for (int i = 15; i >= 0; --i)
          if (!(trap_alloc_ & (1u << i))) { got = i; break; }
      }
      if (got >= 0) trap_alloc_ |= 1u << got;
      D(0) = uint32_t(got);
      return;
    }
    case 348:  // FreeTrap(d0 num)
      if (int32_t(D(0)) >= 0) trap_alloc_ &= ~(1u << (D(0) & 15));
      return;
    case 354:  // AddPort(a1 port)
      m.w8(A(1) + LN_TYPE, NT_MSGPORT);
      new_list(A(1) + MP_MSGLIST);
      enqueue(port_list_, A(1));
      return;
    case 360: remove(A(1)); return;  // RemPort
    case 366: put_msg(A(0), A(1)); return;  // PutMsg
    case 372: D(0) = rem_head(A(0) + MP_MSGLIST); return;  // GetMsg
    case 378: reply_msg(A(1)); return;  // ReplyMsg
    case 384: {  // WaitPort(a0 port)
      uint32_t l = A(0) + MP_MSGLIST;
      uint32_t head = m.r32(l + LH_HEAD);
      if (m.r32(head + LN_SUCC)) { D(0) = head; return; }
      wait(1u << m.r8(A(0) + MP_SIGBIT));
      return;
    }
    case 390: D(0) = find_name(port_list_, m.cstr(A(1))); return;  // FindPort
    case 408:  // OldOpenLibrary(a1 name)
    case 552: {  // OpenLibrary(a1 name, d0 version)
      std::string name = m.cstr(A(1));
      D(0) = find_name(lib_list_, name);
      if (!D(0)) m.log("OpenLibrary(\"%s\") failed\n", name.c_str());
      return;
    }
    case 414: return;  // CloseLibrary
    case 444: {  // OpenDevice(a0 name, d0 unit, a1 ioreq, d1 flags)
      std::string name = m.cstr(A(0));
      if (name == "trackdisk.device" && D(0) == 0) {
        m.w32(A(1) + IO_DEVICE, libs_[LIB_TRACKDISK]);
        m.w32(A(1) + IO_UNIT, td_unit_);
        m.w8(A(1) + IO_ERROR, 0);
        D(0) = 0;
      } else {
        m.log("OpenDevice(\"%s\", %d) failed\n", name.c_str(), int(D(0)));
        m.w8(A(1) + IO_ERROR, uint8_t(IOERR_OPENFAIL));
        D(0) = uint32_t(int32_t(IOERR_OPENFAIL));
      }
      return;
    }
    case 450: return;  // CloseDevice
    case 456:  // DoIO(a1 ioreq)
      m.w8(A(1) + IO_FLAGS, IOF_QUICK);
      begin_io(A(1));
      D(0) = uint32_t(int32_t(int8_t(m.r8(A(1) + IO_ERROR))));
      return;
    case 462:  // SendIO(a1 ioreq)
      m.w8(A(1) + IO_FLAGS, 0);
      begin_io(A(1));
      return;
    case 468:  // CheckIO(a1 ioreq): all requests complete immediately
      D(0) = A(1);
      return;
    case 474: {  // WaitIO(a1 ioreq)
      if (m.r8(A(1) + LN_TYPE) == NT_REPLYMSG && !(m.r8(A(1) + IO_FLAGS) & IOF_QUICK)) remove(A(1));
      D(0) = uint32_t(int32_t(int8_t(m.r8(A(1) + IO_ERROR))));
      return;
    }
    case 480: D(0) = 0; return;  // AbortIO
    case 498: D(0) = 0; return;  // OpenResource: none available
    default: unknown(LIB_EXEC, lvo); return;
  }
}

// ---- trackdisk.device -------------------------------------------------------

void Hle::begin_io(uint32_t io) {
  uint32_t dev = m.r32(io + IO_DEVICE);
  uint16_t cmd = m.r16(io + IO_COMMAND) & 0x7FFF;  // ETD_ commands are the same here
  m.w8(io + LN_TYPE, NT_MESSAGE);
  int8_t err = 0;
  if (dev != libs_[LIB_TRACKDISK]) {
    m.log("BeginIO on unknown device %06x cmd %d\n", dev, cmd);
    err = IOERR_NOCMD;
  } else {
    uint32_t offset = m.r32(io + IO_OFFSET), len = m.r32(io + IO_LENGTH);
    uint32_t data = m.r32(io + IO_DATA);
    switch (cmd) {
      case 2: {  // CMD_READ
        if (m.verbose) m.log("[trackdisk] read %06x+%u -> %06x\n", offset, len, data);
        std::vector<uint8_t> buf(len);
        if (!m.disk.read(offset, len, buf.data())) { err = TDERR_SEEKERROR; break; }
        for (uint32_t i = 0; i < len; ++i) m.w8(data + i, buf[i]);
        m.w32(io + IO_ACTUAL, len);
        m.cyl_ = int(offset / (512 * 22));
        break;
      }
      case 3:    // CMD_WRITE
      case 11: {  // TD_FORMAT
        if (m.verbose) m.log("[trackdisk] write %06x+%u <- %06x\n", offset, len, data);
        std::vector<uint8_t> buf(len);
        for (uint32_t i = 0; i < len; ++i) buf[i] = m.r8(data + i);
        if (!m.disk.write(offset, len, buf.data())) { err = TDERR_SEEKERROR; break; }
        m.w32(io + IO_ACTUAL, len);
        m.cyl_ = int(offset / (512 * 22));
        break;
      }
      case 4: m.disk.flush(); break;  // CMD_UPDATE
      case 5: break;                  // CMD_CLEAR
      case 9:                          // TD_MOTOR
        m.w32(io + IO_ACTUAL, m.motor_ ? 1 : 0);
        m.motor_ = len != 0;
        if (!m.motor_) m.disk.flush();
        break;
      case 10: m.cyl_ = int(offset / (512 * 22)); break;  // TD_SEEK
      case 12: break;                                      // TD_REMOVE
      case 13: m.w32(io + IO_ACTUAL, 1); break;           // TD_CHANGENUM
      case 14: m.w32(io + IO_ACTUAL, 0); break;           // TD_CHANGESTATE: disk in
      case 15: m.w32(io + IO_ACTUAL, 0); break;           // TD_PROTSTATUS: writable
      case 18: m.w32(io + IO_ACTUAL, 1); break;           // TD_GETDRIVETYPE: 3.5" DD
      case 19: m.w32(io + IO_ACTUAL, 160); break;         // TD_GETNUMTRACKS
      default:
        m.log("trackdisk: unsupported command %d\n", cmd);
        err = IOERR_NOCMD;
        break;
    }
  }
  m.w8(io + IO_ERROR, uint8_t(err));
  if (!(m.r8(io + IO_FLAGS) & IOF_QUICK)) reply_msg(io);
  else m.w8(io + LN_TYPE, NT_REPLYMSG);
}

void Hle::trackdisk(int lvo) {
  switch (lvo) {
    case 30: begin_io(A(1)); return;  // BeginIO
    case 36: D(0) = 0; return;        // AbortIO
    default: unknown(LIB_TRACKDISK, lvo); return;
  }
}

// ---- dos / graphics -----------------------------------------------------------

// Set one pixel of a RastPort's BitMap with its pen and draw mode.
void Hle::plot(uint32_t rp, int x, int y) {
  uint32_t bm = m.r32(rp + 4);
  if (!bm) return;
  int bpr = m.r16(bm), rows = m.r16(bm + 2), depth = m.r8(bm + 5);
  if (x < 0 || y < 0 || x >= bpr * 8 || y >= rows) return;
  uint8_t mask = m.r8(rp + 24), pen = m.r8(rp + 25), mode = m.r8(rp + 28);
  if (mode & 4) pen = m.r8(rp + 26);  // INVERSVID
  uint8_t bit = uint8_t(0x80 >> (x & 7));
  for (int p = 0; p < depth && p < 8; ++p) {
    if (!(mask & (1 << p))) continue;
    uint32_t a = m.r32(bm + 8 + uint32_t(p) * 4) + uint32_t(y * bpr + (x >> 3));
    uint8_t v = m.r8(a);
    if (mode & 2) v ^= bit;  // COMPLEMENT
    else v = (pen & (1 << p)) ? (v | bit) : (v & ~bit);
    m.w8(a, v);
  }
}

// ---- AmigaDOS boot and dos.library ---------------------------------------------

namespace {
std::vector<std::string> split_words(const std::string& line) {
  std::vector<std::string> w;
  size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && std::isspace(uint8_t(line[i]))) ++i;
    if (i >= line.size()) break;
    size_t j = i;
    if (line[i] == '"') {
      j = line.find('"', i + 1);
      if (j == std::string::npos) j = line.size();
      w.push_back(line.substr(i + 1, j - i - 1));
      i = j + 1;
      continue;
    }
    while (j < line.size() && !std::isspace(uint8_t(line[j]))) ++j;
    w.push_back(line.substr(i, j - i));
    i = j;
  }
  return w;
}

std::string lower(std::string s) {
  for (auto& c : s) c = char(std::tolower(uint8_t(c)));
  return s;
}
}  // namespace

void Hle::boot_dos() {
  // Kickstart would start dos.library and execute s/startup-sequence in the
  // initial CLI. The game's disk only uses a few commands ("apro1.",
  // "stack 2000", "load it. -s 3000" with LOAD a RunBackground clone,
  // "endcli"), so the script is executed here: each program runs as the
  // CLI's command, the next line starts when it returns.
  std::vector<uint8_t> seq;
  int sb = m.disk.find("s/startup-sequence");
  if (sb < 0 || !m.disk.read_file(sb, seq)) {
    m.log("no s/startup-sequence on the disk\n");
    throw QuitRequested{};
  }
  std::string text(seq.begin(), seq.end());
  size_t pos = 0;
  while (pos < text.size()) {
    size_t nl = text.find('\n', pos);
    script_.push_back(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
    pos = nl == std::string::npos ? text.size() : nl + 1;
  }
  // Debugging: start with the second program (skip the lines before it.).
  if (getenv("HOSTAGES_START_IT"))
    while (script_pos_ < script_.size() && lower(script_[script_pos_]).find("it.") == std::string::npos)
      ++script_pos_;
  cli_ = low_alloc(64);
  current_dir_ = make_lock(880);
  m.w32(task_ + 152, current_dir_);  // pr_CurrentDir
  m.w32(task_ + 172, cli_ >> 2);     // pr_CLI
  if (!next_command()) {
    m.log("s/startup-sequence starts no program\n");
    throw QuitRequested{};
  }
}

bool Hle::next_command() {
  std::string program, args;
  while (script_pos_ < script_.size() && program.empty()) {
    auto w = split_words(script_[script_pos_++]);
    if (w.empty() || w[0][0] == ';') continue;
    std::string cmd = lower(w[0]);
    if (cmd == "stack" && w.size() > 1) { stack_size_ = uint32_t(std::stoul(w[1])); continue; }
    if (cmd == "endcli") return false;
    size_t first = 0;
    if (cmd == "run" || cmd == "load" || cmd == "runbackground") {
      first = 1;
      if (w.size() > 1 && w[1][0] == '-') first = 2;  // RunBackground load delay
    } else if (m.disk.find("c/" + w[0]) >= 0) {
      continue;  // another system command (prompt, ...)
    }
    if (first >= w.size()) continue;
    program = w[first];
    for (size_t i = first + 1; i < w.size(); ++i) args += (i > first + 1 ? " " : "") + w[i];
  }
  if (program.empty()) return false;
  if (m.verbose) m.log("[cli] running \"%s %s\" (stack %u)\n", program.c_str(), args.c_str(),
                       stack_size_);
  uint32_t entry = 0;
  seglist_ = load_seg(program, &entry);
  if (!seglist_) {
    m.log("cannot load %s\n", program.c_str());
    return false;
  }
  uint32_t cmdname = m.r32(cli_ + 16) << 2;
  if (!cmdname) {
    cmdname = low_alloc(40);
    m.w32(cli_ + 16, cmdname >> 2);  // cli_CommandName
  }
  m.w8(cmdname, uint8_t(std::min<size_t>(program.size(), 38)));
  for (size_t i = 0; i < program.size() && i < 38; ++i) m.w8(cmdname + 1 + uint32_t(i), uint8_t(program[i]));
  m.w32(cli_ + 52, stack_size_ / 4);  // cli_DefaultStack (longs)
  m.w32(cli_ + 60, seglist_);         // cli_Module
  m.w32(task_ + 128, seglist_);       // pr_SegList
  m.w32(task_ + 132, stack_size_);    // pr_StackSize

  std::string line = args + "\n";
  static uint32_t argp = 0;
  if (!argp) argp = low_alloc(256);
  for (size_t i = 0; i < line.size() && i < 255; ++i) m.w8(argp + uint32_t(i), uint8_t(line[i]));

  auto& c = m.cpu;
  c.s = false;
  c.osp = SSP_TOP;
  c.a[7] = USP_TOP;
  c.imask = 0;
  c.push32(std::max<uint32_t>(stack_size_, 4000));
  c.push32(ret_stub);  // returning continues the script
  c.d[0] = uint32_t(std::min<size_t>(line.size(), 255));
  c.a[0] = argp;
  c.pc = entry;
  return true;
}

void Hle::program_exit() {
  if (stage2_ || script_.empty()) {
    m.log("program returned to the strap (d0=%d)\n", int(D(0)));
    throw QuitRequested{};
  }
  if (m.verbose)
    m.log("[cli] program returned %d (free: chip %u largest %u, total %u)\n", int(D(0)),
          m.avail(true, false), m.avail(true, true), m.avail(false, false));
  unload_seg(seglist_);
  seglist_ = 0;
  if (!next_command()) {
    m.log("end of s/startup-sequence\n");
    throw QuitRequested{};
  }
}

uint32_t Hle::load_seg(const std::string& path, uint32_t* entry) {
  int b = m.disk.find(path);
  std::vector<uint8_t> raw;
  if (b < 0 || !m.disk.read_file(b, raw)) { ioerr_ = 205; return 0; }
  HunkFile hf;
  if (!hf.parse(raw)) { ioerr_ = 121; return 0; }  // ERROR_FILE_NOT_OBJECT
  // Each segment: [size+8][next BPTR][data...], like LoadSeg's AllocMem'd
  // segments; the segment list BPTR points at the "next" longword.
  std::vector<uint32_t> addrs, sizes;
  for (auto& h : hf.hunks) {
    uint32_t a = m.alloc(h.alloc_size + 8, h.chip, true);
    if (!a) {
      m.log("LoadSeg(%s): out of memory\n", path.c_str());
      for (size_t i = 0; i < addrs.size(); ++i) m.free(addrs[i] - 8, sizes[i] + 8);
      ioerr_ = 103;
      return 0;
    }
    m.w32(a, h.alloc_size + 8);
    addrs.push_back(a + 8);
    sizes.push_back(h.alloc_size);
  }
  for (size_t i = 0; i < hf.hunks.size(); ++i) {
    auto& h = hf.hunks[i];
    uint32_t base = addrs[i];
    m.w32(base - 4, i + 1 < addrs.size() ? (addrs[i + 1] - 4) >> 2 : 0);
    for (uint32_t k = 0; k < h.alloc_size; ++k) m.w8(base + k, h.data[k]);
    for (auto& [target, offs] : h.relocs)
      for (uint32_t o : offs) m.w32(base + o, m.r32(base + o) + addrs[target]);
  }
  if (entry) *entry = addrs[0];
  std::string name = m.disk.entry_name(b);
  m.program_loaded(name, addrs, sizes);
  return (addrs[0] - 4) >> 2;
}

void Hle::unload_seg(uint32_t seglist) {
  for (uint32_t s = seglist; s;) {
    uint32_t a = s << 2;
    uint32_t next = m.r32(a);
    m.free(a - 4, m.r32(a - 4));
    s = next;
  }
}

uint32_t Hle::make_lock(int block) {
  uint32_t a = m.alloc(20, false, true);  // struct FileLock
  m.w32(a + 4, uint32_t(block));          // fl_Key
  m.w32(a + 8, uint32_t(-2));             // fl_Access: SHARED_LOCK
  uint32_t bptr = a >> 2;
  locks_[bptr] = block;
  return bptr;
}

int Hle::lock_block(uint32_t lock) {
  if (!lock) return 880;  // NULL lock: the boot volume's root
  auto it = locks_.find(lock);
  return it == locks_.end() ? -1 : it->second;
}

void Hle::dos(int lvo) {
  auto file = [&](uint32_t fh) -> DosFile* {
    auto it = files_.find(fh);
    return it == files_.end() ? nullptr : &it->second;
  };
  switch (lvo) {
    case 30: {  // Open(d1 name, d2 mode)
      std::string name = m.cstr(D(1));
      int mode = int(D(2));
      DosFile f;
      f.name = name;
      if (mode == 1006) {  // MODE_NEWFILE: kept in memory only
        m.log("dos: Open(\"%s\", MODE_NEWFILE): writes are not saved\n", name.c_str());
        f.write = true;
      } else {
        int b = m.disk.find(name);
        if (b < 0 || m.disk.is_dir(b) || !m.disk.read_file(b, f.data)) {
          if (m.verbose) m.log("[dos] Open(\"%s\") failed\n", name.c_str());
          ioerr_ = 205;  // ERROR_OBJECT_NOT_FOUND
          D(0) = 0;
          return;
        }
      }
      uint32_t fh = m.alloc(44, false, true);  // struct FileHandle
      uint32_t bptr = fh >> 2;
      files_[bptr] = std::move(f);
      if (m.verbose) m.log("[dos] Open(\"%s\", %d) = %08x\n", name.c_str(), mode, bptr);
      D(0) = bptr;
      return;
    }
    case 36: {  // Close(d1 fh)
      if (files_.erase(D(1))) m.free(D(1) << 2, 44);
      D(0) = 1;
      return;
    }
    case 42: {  // Read(d1 fh, d2 buf, d3 len)
      DosFile* f = file(D(1));
      if (!f) { D(0) = uint32_t(-1); return; }
      uint32_t n = std::min<uint32_t>(D(3), uint32_t(f->data.size()) - std::min<uint32_t>(f->pos, uint32_t(f->data.size())));
      for (uint32_t i = 0; i < n; ++i) m.w8(D(2) + i, f->data[f->pos + i]);
      f->pos += n;
      D(0) = n;
      return;
    }
    case 48: {  // Write(d1 fh, d2 buf, d3 len)
      DosFile* f = file(D(1));
      if (!f) {  // Output(): console
        for (uint32_t i = 0; i < D(3); ++i) fputc(m.r8(D(2) + i), stderr);
        D(0) = D(3);
        return;
      }
      if (f->pos + D(3) > f->data.size()) f->data.resize(f->pos + D(3));
      for (uint32_t i = 0; i < D(3); ++i) f->data[f->pos + i] = m.r8(D(2) + i);
      f->pos += D(3);
      D(0) = D(3);
      return;
    }
    case 54: D(0) = 0; return;  // Input: none (stdin of a background task)
    case 60: D(0) = 0; return;  // Output: NIL:
    case 66: {  // Seek(d1 fh, d2 pos, d3 mode) → old position
      DosFile* f = file(D(1));
      if (!f) { D(0) = uint32_t(-1); return; }
      int64_t base = int32_t(D(3)) < 0 ? 0 : D(3) == 0 ? f->pos : int64_t(f->data.size());
      int64_t np = base + int32_t(D(2));
      if (np < 0 || np > int64_t(f->data.size())) { D(0) = uint32_t(-1); ioerr_ = 219; return; }
      D(0) = f->pos;
      f->pos = uint32_t(np);
      return;
    }
    case 84: {  // Lock(d1 name, d2 mode)
      int b = m.disk.find(m.cstr(D(1)));
      if (b < 0) { ioerr_ = 205; D(0) = 0; return; }
      D(0) = make_lock(b);
      return;
    }
    case 90:  // UnLock(d1 lock)
      if (D(1) != current_dir_ && locks_.erase(D(1))) m.free(D(1) << 2, 20);
      return;
    case 96: {  // DupLock(d1 lock)
      int b = lock_block(D(1));
      D(0) = b < 0 ? 0 : make_lock(b);
      return;
    }
    case 102:    // Examine(d1 lock, d2 fib)
    case 108: {  // ExNext(d1 lock, d2 fib)
      int b = lock_block(D(1));
      uint32_t fib = D(2);
      if (b < 0) { D(0) = 0; ioerr_ = 205; return; }
      int e = b;
      if (lvo == 108) {
        auto entries = m.disk.list_dir(b);
        int& idx = exnext_[fib];
        if (idx >= int(entries.size())) { exnext_.erase(fib); D(0) = 0; ioerr_ = 232; return; }
        e = entries[size_t(idx++)];
      } else {
        exnext_[fib] = 0;
      }
      bool dir = m.disk.is_dir(e);
      for (uint32_t i = 0; i < 260; ++i) m.w8(fib + i, 0);
      m.w32(fib, uint32_t(e));                              // fib_DiskKey
      m.w32(fib + 4, dir ? 2 : uint32_t(-3));               // fib_DirEntryType
      std::string n = m.disk.entry_name(e);
      m.w8(fib + 8, uint8_t(n.size()));                     // fib_FileName (BSTR)
      for (size_t i = 0; i < n.size(); ++i) m.w8(fib + 9 + uint32_t(i), uint8_t(n[i]));
      m.w32(fib + 120, dir ? 2 : uint32_t(-3));             // fib_EntryType
      uint32_t size = dir ? 0 : m.disk.file_size(e);
      m.w32(fib + 124, size);                               // fib_Size
      m.w32(fib + 128, (size + 487) / 488);                 // fib_NumBlocks
      D(0) = 1;
      return;
    }
    case 114: {  // Info(d1 lock, d2 infodata)
      uint32_t id = D(2);
      for (uint32_t i = 0; i < 36; ++i) m.w8(id + i, 0);
      m.w32(id + 8, 80);           // ID_WRITE_PROTECTED
      m.w32(id + 12, 1760);        // NumBlocks
      m.w32(id + 16, 1760);        // NumBlocksUsed
      m.w32(id + 20, 488);         // BytesPerBlock
      m.w32(id + 24, 0x444F5300);  // ID_DOS_DISK
      D(0) = 1;
      return;
    }
    case 126: {  // CurrentDir(d1 lock) → old
      D(0) = current_dir_;
      current_dir_ = D(1);
      m.w32(task_ + 152, current_dir_);
      return;
    }
    case 132: D(0) = ioerr_; return;  // IoErr
    case 144:  // Exit(d1 code): back to the CLI
      D(0) = D(1);
      program_exit();
      return;
    case 150: D(0) = load_seg(m.cstr(D(1))); return;  // LoadSeg(d1 name)
    case 156: unload_seg(D(1)); D(0) = 1; return;      // UnLoadSeg(d1 seglist)
    case 192: {  // DateStamp(d1 ds): days, minutes, ticks since 1978
      m.w32(D(1), 0);
      m.w32(D(1) + 4, 0);
      m.w32(D(1) + 8, uint32_t(m.frame_count() % 3000));
      D(0) = D(1);
      return;
    }
    case 198:  // Delay(d1 ticks): retried until enough frames have passed
      if (!delay_until_) delay_until_ = m.frame_count() + std::max<uint32_t>(D(1), 1);
      if (m.frame_count() < delay_until_) {
        m.cpu.pc = m.cpu.op_pc;
        m.cpu.cycles = std::max(m.cpu.cycles, m.line_start_ + m.line_cycles_);
        return;
      }
      delay_until_ = 0;
      return;
    case 210: {  // ParentDir(d1 lock)
      int b = lock_block(D(1));
      D(0) = b < 0 || b == 880 ? 0 : make_lock(m.disk.parent(b));
      return;
    }
    case 216: D(0) = 0; return;  // IsInteractive
    default: unknown(LIB_DOS, lvo); return;
  }
}

// ---- intuition ------------------------------------------------------------------

uint32_t Hle::open_screen(uint32_t ns) {
  int16_t top = int16_t(m.r16(ns + 2));
  uint16_t width = m.r16(ns + 4), height = m.r16(ns + 6), depth = m.r16(ns + 8);
  uint16_t modes = m.r16(ns + 12), type = m.r16(ns + 14);
  uint32_t custom_bm = m.r32(ns + 28);
  if (int16_t(width) <= 0) width = 320;
  if (int16_t(height) <= 0) height = 256;
  uint32_t sc = m.alloc(SC_SIZE, false, true);
  m.w16(sc + 10, uint16_t(top));
  m.w16(sc + SC_WIDTH, width);
  m.w16(sc + SC_HEIGHT, height);
  m.w32(sc + 22, m.r32(ns + 20));  // Title
  m.w32(sc + 26, m.r32(ns + 20));  // DefaultTitle
  m.w8(sc + 30, 10);               // BarHeight
  Screen s;
  s.addr = sc;
  // BitMap: the custom one (CUSTOMBITMAP) or our own planes.
  uint32_t bm = sc + SC_BITMAP;
  if ((type & 0x40) && custom_bm) {
    for (uint32_t i = 0; i < 40; ++i) m.w8(bm + i, m.r8(custom_bm + i));
  } else {
    uint32_t bpr = ((width + 15u) >> 4) * 2;
    m.w16(bm, uint16_t(bpr));
    m.w16(bm + 2, height);
    m.w8(bm + 5, uint8_t(depth));
    for (uint32_t p = 0; p < depth && p < 8; ++p) {
      uint32_t plane = m.alloc(bpr * height, true, true);
      if (!plane) { m.log("OpenScreen: out of chip memory\n"); return 0; }
      m.w32(bm + 8 + p * 4, plane);
    }
    s.own_bitmap = true;
  }
  // RastPort on the BitMap
  uint32_t rp = sc + SC_RASTPORT;
  m.w32(rp + 4, bm);
  m.w8(rp + 24, 0xFF);
  m.w8(rp + 25, 1);
  m.w8(rp + 28, 1);
  m.w16(rp + 34, 0xFFFF);
  // ViewPort with RasInfo and a 32 entry ColorMap
  uint32_t vp = sc + SC_VIEWPORT;
  uint32_t ri = m.alloc(12, false, true);
  m.w32(ri + 4, (type & 0x40) && custom_bm ? custom_bm : bm);
  uint32_t cm = m.alloc(8 + 64, false, true);
  m.w16(cm + 2, 32);
  m.w32(cm + 4, cm + 8);
  static const uint16_t wb[4] = {0x05A, 0xFFF, 0x002, 0xF80};  // 1.3 Workbench colours
  for (uint32_t i = 0; i < 4; ++i) m.w16(cm + 8 + i * 2, wb[i]);
  m.w32(vp + VP_COLORMAP, cm);
  m.w16(vp + VP_DWIDTH, width);
  m.w16(vp + VP_DHEIGHT, height);
  m.w16(vp + VP_DYOFFSET, uint16_t(top));
  m.w16(vp + VP_MODES, modes);
  m.w32(vp + VP_RASINFO, ri);
  s.copper = m.alloc(4 * 128, true, true);  // room for the ~72 instructions
  screens_.push_back(s);
  build_screen_copper(screens_.back());
  show_front_screen();
  if (m.verbose)
    m.log("[intuition] OpenScreen %ux%ux%u modes %04x type %04x -> %06x\n", width, height,
          depth, modes, type, sc);
  return sc;
}

void Hle::build_screen_copper(Screen& s) {
  // Same layout as graphics.library's MrgCop output, which the game relies
  // on: it double-buffers by finding the BPL4PTL move (searching from 64
  // bytes into LOFCprList->start) and patching the plane pointers before
  // it. So: 8 sprite pointers, then the display set-up and bitplane
  // pointers, then the colours.
  uint32_t vp = s.addr + SC_VIEWPORT;
  uint32_t ri = m.r32(vp + VP_RASINFO);
  uint32_t bm = ri ? m.r32(ri + 4) : 0;
  if (!bm) return;
  uint16_t modes = m.r16(vp + VP_MODES);
  int depth = m.r8(bm + 5);
  int bpr = m.r16(bm);
  int height = m.r16(vp + VP_DHEIGHT);
  int width = m.r16(vp + VP_DWIDTH);
  bool hires = modes & V_HIRES;
  int top = 0x2C + int16_t(m.r16(vp + VP_DYOFFSET));
  int rx = int16_t(m.r16(ri + 8)), ry = int16_t(m.r16(ri + 10));
  int fetch = width / 8;  // bytes fetched per line
  if (!blank_sprite_) blank_sprite_ = m.alloc(8, true, true);
  uint32_t p = s.copper;
  auto move = [&](uint16_t reg, uint16_t v) { m.w16(p, reg); m.w16(p + 2, v); p += 4; };
  for (int i = 0; i < 8; ++i) {
    move(uint16_t(0x120 + i * 4), uint16_t(blank_sprite_ >> 16));
    move(uint16_t(0x122 + i * 4), uint16_t(blank_sprite_));
  }
  move(0x100, 0x0200);
  move(0x08E, uint16_t((top & 0xFF) << 8 | 0x81));
  move(0x090, uint16_t(((top + height) & 0xFF) << 8 | 0xC1));
  move(0x092, hires ? 0x3C : 0x38);
  move(0x094, hires ? 0xD4 : 0xD0);
  move(0x102, 0);
  move(0x104, 0x24);
  move(0x108, uint16_t(bpr - fetch));
  move(0x10A, uint16_t(bpr - fetch));
  for (int i = 0; i < depth && i < 6; ++i) {
    uint32_t plane = m.r32(bm + 8 + uint32_t(i) * 4) + uint32_t(ry * bpr + (rx >> 3));
    move(uint16_t(0xE0 + i * 4), uint16_t(plane >> 16));
    move(uint16_t(0xE2 + i * 4), uint16_t(plane));
  }
  s.colors = p;
  uint32_t cm = m.r32(vp + VP_COLORMAP);
  int ncol = cm ? std::min<int>(m.r16(cm + 2), 32) : 0;
  for (int i = 0; i < ncol; ++i) move(uint16_t(0x180 + i * 2), m.r16(m.r32(cm + 4) + uint32_t(i) * 2));
  // Planes are switched on at the top of the screen, off at its end.
  uint16_t con0 = uint16_t(depth << 12 | 0x200 | (hires ? 0x8000 : 0) |
                           (modes & V_HAM ? 0x800 : 0) | (modes & V_DUALPF ? 0x400 : 0));
  int bottom = top + height;
  m.w16(p, uint16_t(((top - 1) & 0xFF) << 8 | 0x01)); m.w16(p + 2, 0xFFFE); p += 4;
  move(0x100, con0);
  if (bottom > 255) { m.w16(p, 0xFFDF); m.w16(p + 2, 0xFFFE); p += 4; }
  m.w16(p, uint16_t((bottom & 0xFF) << 8 | 0x01)); m.w16(p + 2, 0xFFFE); p += 4;
  move(0x100, 0x0200);
  m.w32(p, 0xFFFFFFFE);
}

void Hle::update_screen_colors(uint32_t vp) {
  // Colour changes rewrite only the colour moves, so plane pointers the
  // program patched into the list stay as they are.
  for (auto& s : screens_) {
    if (s.addr + SC_VIEWPORT != vp || !s.colors) continue;
    uint32_t cm = m.r32(vp + VP_COLORMAP);
    int ncol = cm ? std::min<int>(m.r16(cm + 2), 32) : 0;
    for (int i = 0; i < ncol; ++i)
      m.w16(s.colors + uint32_t(i) * 4 + 2, m.r16(m.r32(cm + 4) + uint32_t(i) * 2));
  }
}

void Hle::rebuild_viewport(uint32_t vp) {
  for (auto& s : screens_)
    if (s.addr + SC_VIEWPORT == vp) build_screen_copper(s);
}

void Hle::show_front_screen() {
  if (screens_.empty()) return;
  // GfxBase->ActiView: Intuition's View, whose ViewPort is the front
  // screen's and whose LOFCprList holds the displayed copper list.
  if (!view_) {
    view_ = m.alloc(18, false, true);
    cprlist_ = m.alloc(12, false, true);
    m.w32(libs_[LIB_GFX] + 34, view_);
  }
  m.w32(view_, screens_.back().addr + SC_VIEWPORT);
  m.w32(view_ + 4, cprlist_);
  m.w32(cprlist_ + 4, screens_.back().copper);
  m.w16(cprlist_ + 8, 128);
  uint32_t list = screens_.back().copper;
  m.w16(copinit_ + 6, uint16_t(list >> 16));
  m.w16(copinit_ + 10, uint16_t(list));
  m.w32(libs_[LIB_GFX] + 50, list);  // LOFlist
  m.custom_write(0x080, uint16_t(copinit_ >> 16));
  m.custom_write(0x082, uint16_t(copinit_));
  m.custom_write(0x096, 0x8000 | 0x0200 | 0x0100 | 0x0080);  // DMAEN | BPLEN | COPEN
}

void Hle::before_frame() {}

void Hle::intuition(int lvo) {
  switch (lvo) {
    case 66: {  // CloseScreen(a0)
      for (size_t i = 0; i < screens_.size(); ++i)
        if (screens_[i].addr == A(0)) {
          screens_.erase(screens_.begin() + long(i));
          break;
        }
      show_front_screen();
      D(0) = 1;
      return;
    }
    case 72: return;             // CloseWindow
    case 78: D(0) = 1; return;   // CloseWorkBench
    case 198: D(0) = open_screen(A(0)); return;  // OpenScreen(a0 newscreen)
    case 204: {  // OpenWindow(a0 newwindow)
      uint32_t nw = A(0);
      uint32_t w = m.alloc(136, false, true);
      m.w16(w + 4, m.r16(nw));       // LeftEdge
      m.w16(w + 6, m.r16(nw + 2));   // TopEdge
      m.w16(w + 8, m.r16(nw + 4));   // Width
      m.w16(w + 10, m.r16(nw + 6));  // Height
      uint32_t flags = m.r32(nw + 14), idcmp = m.r32(nw + 10);
      m.w32(w + 24, flags);
      m.w32(w + 82, idcmp);
      uint32_t sc = m.r32(nw + 30);
      if (!sc && !screens_.empty()) sc = screens_.back().addr;
      m.w32(w + 46, sc);                               // WScreen
      m.w32(w + 50, sc ? sc + SC_RASTPORT : 0);         // RPort
      if (idcmp) {  // UserPort
        uint32_t port = m.alloc(34, false, true);
        m.w8(port + LN_TYPE, NT_MSGPORT);
        m.w8(port + MP_SIGBIT, 9);
        m.w32(port + MP_SIGTASK, task_);
        new_list(port + MP_MSGLIST);
        m.w32(w + 86, port);
      }
      if (m.verbose)
        m.log("[intuition] OpenWindow flags %08x idcmp %08x -> %06x\n", flags, idcmp, w);
      D(0) = w;
      return;
    }
    case 210: D(0) = 1; return;  // OpenWorkBench
    case 252: case 246: return;  // ScreenToFront / ScreenToBack
    case 300: D(0) = m.r32(A(0) + 46) + SC_VIEWPORT; return;  // ViewPortAddress(a0 window)
    case 378: rebuild_viewport(A(0) + SC_VIEWPORT); return;    // MakeScreen(a0)
    case 384: case 390:  // RemakeDisplay / RethinkDisplay
      for (auto& s : screens_) build_screen_copper(s);
      show_front_screen();
      return;
    default: unknown(LIB_INTUITION, lvo); return;
  }
}

void Hle::mathffp(int lvo) { unknown(LIB_MATHFFP, lvo); }

void Hle::gfx(int lvo) {
  // RastPort: +4 BitMap, +24 Mask, +25 FgPen, +26 BgPen, +28 DrawMode
  // BitMap: +0 BytesPerRow, +2 Rows, +5 Depth, +8 Planes[8]
  switch (lvo) {
    case 30: {  // BltBitMap(a0 src, d0 sx, d1 sy, a1 dst, d2 dx, d3 dy, d4 w, d5 h,
                //           d6 minterm, d7 mask, a2 temp) → planes
      uint32_t src = A(0), dst = A(1);
      int sx = int16_t(D(0)), sy = int16_t(D(1)), dx = int16_t(D(2)), dy = int16_t(D(3));
      int w = int16_t(D(4)), h = int16_t(D(5));
      uint8_t lf = uint8_t(D(6)), mask = uint8_t(D(7));
      int sbpr = m.r16(src), dbpr = m.r16(dst);
      int depth = std::min(m.r8(src + 5), m.r8(dst + 5));
      int planes = 0;
      std::vector<uint8_t> row(size_t(std::max(w, 0)));
      for (int p = 0; p < depth && p < 8; ++p) {
        if (!(mask & (1 << p))) continue;
        ++planes;
        uint32_t sp = m.r32(src + 8 + uint32_t(p) * 4), dp = m.r32(dst + 8 + uint32_t(p) * 4);
        // Copy in the direction that is safe for overlapping areas.
        bool up = dp == sp && dy > sy;
        for (int i = 0; i < h; ++i) {
          int y = up ? h - 1 - i : i;
          for (int x = 0; x < w; ++x) {
            int px = sx + x;
            row[size_t(x)] = (m.r8(sp + uint32_t((sy + y) * sbpr + (px >> 3))) >> (7 - (px & 7))) & 1;
          }
          for (int x = 0; x < w; ++x) {
            int px = dx + x;
            uint32_t a = dp + uint32_t((dy + y) * dbpr + (px >> 3));
            uint8_t bit = uint8_t(0x80 >> (px & 7));
            uint8_t v = m.r8(a);
            int b = row[size_t(x)], c = (v & bit) ? 1 : 0;
            // minterm with A = 1 (the blitter's A channel is the mask)
            bool d = (lf >> (4 | b << 1 | c)) & 1;
            m.w8(a, d ? (v | bit) : (v & ~bit));
          }
        }
      }
      D(0) = uint32_t(planes);
      return;
    }
    case 192: {  // LoadRGB4(a0 vp, a1 colors, d0 count)
      if (!A(0)) return;
      uint32_t cm = m.r32(A(0) + VP_COLORMAP);
      if (!cm) return;
      int n = std::min<int>(int(D(0) & 0xFFFF), m.r16(cm + 2));
      for (int i = 0; i < n; ++i)
        m.w16(m.r32(cm + 4) + uint32_t(i) * 2, m.r16(A(1) + uint32_t(i) * 2) & 0xFFF);
      update_screen_colors(A(0));
      return;
    }
    case 198: {  // InitRastPort(a1)
      uint32_t rp = A(1);
      for (uint32_t i = 0; i < 100; ++i) m.w8(rp + i, 0);
      m.w8(rp + 24, 0xFF);     // Mask
      m.w8(rp + 25, 0xFF);     // FgPen
      m.w8(rp + 27, 0xFF);     // AOlPen
      m.w8(rp + 28, 1);        // DrawMode JAM2
      m.w16(rp + 34, 0xFFFF);  // LinePtrn
      return;
    }
    case 228: m.blit_finish(); return;  // WaitBlit
    case 234: {  // SetRast(a1 rp, d0 pen)
      uint32_t bm = m.r32(A(1) + 4);
      if (!bm) return;
      uint32_t size = uint32_t(m.r16(bm)) * m.r16(bm + 2);
      for (int p = 0; p < m.r8(bm + 5) && p < 8; ++p) {
        uint32_t plane = m.r32(bm + 8 + uint32_t(p) * 4);
        uint8_t v = (D(0) >> p) & 1 ? 0xFF : 0;
        for (uint32_t i = 0; i < size; ++i) m.w8(plane + i, v);
      }
      return;
    }
    case 270:  // WaitTOF: retried until the next frame
      if (!wait_frame_) wait_frame_ = m.frame_count() + 1;
      if (m.frame_count() < wait_frame_) {
        m.cpu.pc = m.cpu.op_pc;
        m.cpu.cycles = std::max(m.cpu.cycles, m.line_start_ + m.line_cycles_);
        return;
      }
      wait_frame_ = 0;
      return;
    case 288: {  // SetRGB4(a0 vp, d0 n, d1 r, d2 g, d3 b)
      if (!A(0)) return;
      uint32_t cm = m.r32(A(0) + VP_COLORMAP);
      if (cm && (D(0) & 0xFFFF) < m.r16(cm + 2))
        m.w16(m.r32(cm + 4) + (D(0) & 0xFFFF) * 2,
              uint16_t((D(1) & 15) << 8 | (D(2) & 15) << 4 | (D(3) & 15)));
      update_screen_colors(A(0));
      return;
    }
    case 240:  // Move(a1 rp, d0 x, d1 y)
      m.w16(A(1) + 36, uint16_t(D(0)));
      m.w16(A(1) + 38, uint16_t(D(1)));
      return;
    case 246: {  // Draw(a1 rp, d0 x, d1 y): line from the current position
      uint32_t rp = A(1);
      int x0 = int16_t(m.r16(rp + 36)), y0 = int16_t(m.r16(rp + 38));
      int x1 = int16_t(D(0)), y1 = int16_t(D(1));
      int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
      int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
      for (;;) {
        plot(rp, x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
      }
      m.w16(rp + 36, uint16_t(x1));
      m.w16(rp + 38, uint16_t(y1));
      return;
    }
    case 306: {  // RectFill(a1 rp, d0 xmin, d1 ymin, d2 xmax, d3 ymax)
      int x0 = int16_t(D(0)), y0 = int16_t(D(1)), x1 = int16_t(D(2)), y1 = int16_t(D(3));
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) plot(A(1), x, y);
      return;
    }
    case 324: plot(A(1), int16_t(D(0)), int16_t(D(1))); D(0) = 0; return;  // WritePixel
    case 342: m.w8(A(1) + 25, uint8_t(D(0))); return;  // SetAPen(a1 rp, d0 pen)
    case 348: m.w8(A(1) + 26, uint8_t(D(0))); return;  // SetBPen
    case 354: m.w8(A(1) + 28, uint8_t(D(0))); return;  // SetDrMd(a1 rp, d0 mode)
    case 390: {  // InitBitMap(a0 bm, d0 depth, d1 width, d2 height)
      uint32_t bm = A(0);
      m.w16(bm + 0, uint16_t(((D(1) + 15) >> 4) * 2));
      m.w16(bm + 2, uint16_t(D(2)));
      m.w8(bm + 4, 0);
      m.w8(bm + 5, uint8_t(D(0)));
      m.w16(bm + 6, 0);
      return;
    }
    case 456: m.blit_finish(); return;  // OwnBlitter: the blitter is idle afterwards
    case 462: return;                    // DisownBlitter
    case 492:  // AllocRaster(d0 width, d1 height)
      D(0) = m.alloc(((D(0) + 15) >> 4) * 2 * D(1), true, false);
      return;
    case 498:  // FreeRaster(a0 p, d0 width, d1 height)
      m.free(A(0), ((D(0) + 15) >> 4) * 2 * D(1));
      return;
    default: unknown(LIB_GFX, lvo); return;
  }
}


bool Hle::save_state(FILE* f) const {
  auto wr_u32 = [&](uint32_t v) { return fwrite(&v, 1, 4, f) == 4; };
  auto wr_u64 = [&](uint64_t v) { return fwrite(&v, 1, 8, f) == 8; };
  auto wr_i32 = [&](int32_t v) { return fwrite(&v, 1, 4, f) == 4; };
  auto wr_str = [&](const std::string& s) {
    if (!wr_u32(uint32_t(s.size()))) return false;
    return s.empty() || fwrite(s.data(), 1, s.size(), f) == s.size();
  };
  auto wr_bytes = [&](const std::vector<uint8_t>& v) {
    if (!wr_u32(uint32_t(v.size()))) return false;
    return v.empty() || fwrite(v.data(), 1, v.size(), f) == v.size();
  };

  if (!wr_u32(ret_stub) || !wr_u32(heap_start) || !wr_u32(stage2_)) return false;
  if (!wr_u32(uint32_t(script_.size()))) return false;
  for (const auto& s : script_)
    if (!wr_str(s)) return false;
  if (!wr_u32(uint32_t(script_pos_)) || !wr_u32(stack_size_) || !wr_u32(seglist_) || !wr_u32(cli_))
    return false;

  if (!wr_u32(uint32_t(screens_.size()))) return false;
  for (const auto& s : screens_) {
    if (!wr_u32(s.addr) || !wr_u32(s.copper) || !wr_u32(s.colors)) return false;
    uint8_t ob = s.own_bitmap ? 1 : 0;
    if (fwrite(&ob, 1, 1, f) != 1) return false;
  }
  if (!wr_u32(view_) || !wr_u32(cprlist_) || !wr_u32(copinit_) || !wr_u32(blank_sprite_)) return false;

  if (!wr_u32(uint32_t(files_.size()))) return false;
  for (const auto& [k, file] : files_) {
    if (!wr_u32(k) || !wr_bytes(file.data) || !wr_u32(file.pos)) return false;
    uint8_t w = file.write ? 1 : 0;
    if (fwrite(&w, 1, 1, f) != 1) return false;
    if (!wr_str(file.name)) return false;
  }
  if (!wr_u32(uint32_t(locks_.size()))) return false;
  for (const auto& [k, v] : locks_)
    if (!wr_u32(k) || !wr_i32(int32_t(v))) return false;
  if (!wr_u32(uint32_t(exnext_.size()))) return false;
  for (const auto& [k, v] : exnext_)
    if (!wr_u32(k) || !wr_i32(int32_t(v))) return false;
  if (!wr_u32(ioerr_) || !wr_u32(current_dir_)) return false;
  if (!wr_u64(delay_until_) || !wr_u64(wait_frame_)) return false;

  if (!wr_u32(low_next_)) return false;
  for (int i = 0; i < LIB_COUNT; ++i)
    if (!wr_u32(libs_[i])) return false;
  if (!wr_u32(task_) || !wr_u32(td_unit_) || !wr_u32(port_list_) || !wr_u32(lib_list_)) return false;
  if (!wr_u32(mem_headers_[0]) || !wr_u32(mem_headers_[1])) return false;
  if (!wr_u32(sig_alloc_) || !wr_u32(sig_recvd_) || !wr_u32(trap_alloc_)) return false;
  if (!wr_i32(int32_t(id_nest_))) return false;
  for (int i = 0; i < 16; ++i) {
    if (!wr_u32(uint32_t(servers_[i].size()))) return false;
    for (uint32_t s : servers_[i])
      if (!wr_u32(s)) return false;
  }
  for (int i = 0; i < 16; ++i)
    if (!wr_u32(vectors_[i])) return false;
  return true;
}

bool Hle::load_state(FILE* f) {
  auto rd_u32 = [&](uint32_t& v) { return fread(&v, 1, 4, f) == 4; };
  auto rd_u64 = [&](uint64_t& v) { return fread(&v, 1, 8, f) == 8; };
  auto rd_i32 = [&](int32_t& v) { return fread(&v, 1, 4, f) == 4; };
  auto rd_str = [&](std::string& s) {
    uint32_t n = 0;
    if (!rd_u32(n) || n > 16 * 1024 * 1024) return false;
    s.resize(n);
    return n == 0 || fread(s.data(), 1, n, f) == n;
  };
  auto rd_bytes = [&](std::vector<uint8_t>& v) {
    uint32_t n = 0;
    if (!rd_u32(n) || n > 64 * 1024 * 1024) return false;
    v.resize(n);
    return n == 0 || fread(v.data(), 1, n, f) == n;
  };

  if (!rd_u32(ret_stub) || !rd_u32(heap_start) || !rd_u32(stage2_)) return false;
  uint32_t nscript = 0;
  if (!rd_u32(nscript) || nscript > 256) return false;
  script_.resize(nscript);
  for (auto& s : script_)
    if (!rd_str(s)) return false;
  uint32_t spos = 0;
  if (!rd_u32(spos) || !rd_u32(stack_size_) || !rd_u32(seglist_) || !rd_u32(cli_)) return false;
  script_pos_ = spos;

  uint32_t nscreens = 0;
  if (!rd_u32(nscreens) || nscreens > 16) return false;
  screens_.resize(nscreens);
  for (auto& s : screens_) {
    if (!rd_u32(s.addr) || !rd_u32(s.copper) || !rd_u32(s.colors)) return false;
    uint8_t ob = 0;
    if (fread(&ob, 1, 1, f) != 1) return false;
    s.own_bitmap = ob != 0;
  }
  if (!rd_u32(view_) || !rd_u32(cprlist_) || !rd_u32(copinit_) || !rd_u32(blank_sprite_)) return false;

  uint32_t nfiles = 0;
  if (!rd_u32(nfiles) || nfiles > 256) return false;
  files_.clear();
  for (uint32_t i = 0; i < nfiles; ++i) {
    uint32_t k = 0;
    DosFile file;
    if (!rd_u32(k) || !rd_bytes(file.data) || !rd_u32(file.pos)) return false;
    uint8_t w = 0;
    if (fread(&w, 1, 1, f) != 1) return false;
    file.write = w != 0;
    if (!rd_str(file.name)) return false;
    files_[k] = std::move(file);
  }
  uint32_t nlocks = 0;
  if (!rd_u32(nlocks) || nlocks > 256) return false;
  locks_.clear();
  for (uint32_t i = 0; i < nlocks; ++i) {
    uint32_t k = 0;
    int32_t v = 0;
    if (!rd_u32(k) || !rd_i32(v)) return false;
    locks_[k] = int(v);
  }
  uint32_t nex = 0;
  if (!rd_u32(nex) || nex > 256) return false;
  exnext_.clear();
  for (uint32_t i = 0; i < nex; ++i) {
    uint32_t k = 0;
    int32_t v = 0;
    if (!rd_u32(k) || !rd_i32(v)) return false;
    exnext_[k] = int(v);
  }
  if (!rd_u32(ioerr_) || !rd_u32(current_dir_)) return false;
  if (!rd_u64(delay_until_) || !rd_u64(wait_frame_)) return false;

  if (!rd_u32(low_next_)) return false;
  for (int i = 0; i < LIB_COUNT; ++i)
    if (!rd_u32(libs_[i])) return false;
  if (!rd_u32(task_) || !rd_u32(td_unit_) || !rd_u32(port_list_) || !rd_u32(lib_list_)) return false;
  if (!rd_u32(mem_headers_[0]) || !rd_u32(mem_headers_[1])) return false;
  if (!rd_u32(sig_alloc_) || !rd_u32(sig_recvd_) || !rd_u32(trap_alloc_)) return false;
  int32_t idn = 0;
  if (!rd_i32(idn)) return false;
  id_nest_ = int(idn);
  for (int i = 0; i < 16; ++i) {
    uint32_t n = 0;
    if (!rd_u32(n) || n > 64) return false;
    servers_[i].resize(n);
    for (uint32_t& s : servers_[i])
      if (!rd_u32(s)) return false;
  }
  for (int i = 0; i < 16; ++i)
    if (!rd_u32(vectors_[i])) return false;
  return true;
}

}  // namespace amiga
