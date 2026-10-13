// AmigaOS high-level emulation: exec, trackdisk.device and the boot strap,
// as far as the game (bootblock, stage-2 loader and the two programs) uses
// them. Everything else of the game talks to the hardware directly.
#include "amiga/hle.hpp"

#include <algorithm>
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

// Memory map: vectors and exec structures in low chip RAM; like Kickstart
// on an A500 with a trapdoor expansion, the stacks live in slow RAM so the
// game gets as much chip RAM as possible (the assault part needs ~470 KB).
constexpr uint32_t LOW_END = 0x2000;
constexpr uint32_t SSP_TOP = SLOW_BASE + 0x4000;
constexpr uint32_t USP_LOW = SSP_TOP;
constexpr uint32_t USP_TOP = SSP_TOP + 0x8000;

const char* lib_name(int lib) {
  static const char* names[] = {"exec", "dos", "graphics", "trackdisk"};
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
  // An empty system copper list for copinit/LOFlist.
  uint32_t cop = low_alloc(8);
  m.w32(cop, 0x01000200);   // BPLCON0 = colour, no planes
  m.w32(cop + 4, 0xFFFFFFFE);
  m.w32(gfx + 38, cop);     // copinit
  m.w32(gfx + 50, cop);     // LOFlist
  m.w16(gfx + 206, 4);      // DisplayFlags: PAL
  m.w16(gfx + 212, 311);    // MaxDisplayRow
  m.w16(gfx + 216, 256);    // NormalDisplayRows
  m.w16(gfx + 218, 640);    // NormalDisplayColumns
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

  // CIA set-up done by Kickstart: keyboard serial interrupt enabled.
  m.cia_[0].icr_mask = Cia::ICR_SP;
  m.cs_.intena_ = INT_INTEN | INT_PORTS | INT_VERTB | INT_EXTER | INT_SOFT;

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
      uint32_t r = size ? m.alloc(size, req & MEMF_CHIP, req & MEMF_CLEAR) : 0;
      if (req & MEMF_FAST) r = 0;  // no fast RAM in this machine
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
      if (req & MEMF_FAST) { D(0) = 0; return; }
      D(0) = m.avail(req & MEMF_CHIP, req & MEMF_LARGEST);
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

void Hle::dos(int lvo) { unknown(LIB_DOS, lvo); }

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
    default: unknown(LIB_GFX, lvo); return;
  }
}

}  // namespace amiga
