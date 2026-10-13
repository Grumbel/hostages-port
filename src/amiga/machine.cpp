// Machine: memory map, boot, scheduler and 68k<->host call glue.
#include "amiga/machine.hpp"
#include "amiga/hle.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <cstring>

namespace amiga {

Machine::Machine()
    : chip_(CHIP_SIZE), slow_(SLOW_SIZE), fb_(FB_W * FB_H), view_(FB_W * VIEW_H) {
  ram = chip_.data();
  ram_size = CHIP_SIZE;
  cpu.bus = this;
  cpu.line_a = [this](m68k::Cpu&, uint16_t op) { return line_a(op); };
  cpu.fatal = [this](m68k::Cpu& c, int vec) {
    log("FATAL: exception vector %d at pc=%06x (op_pc=%06x)\n", vec, c.pc, c.op_pc);
    for (int i = 0; i < 8; ++i) log("  d%d=%08x a%d=%08x\n", i, c.d[i], i, c.a[i]);
    throw QuitRequested{};
  };
  hle_ = std::make_unique<Hle>(*this);
  // HOSTAGES_WATCHPC=hexaddr: log every change of a byte with the PC of the
  // instruction that made it (debugging; slows the interpreter down).
  if (const char* w = getenv("HOSTAGES_WATCHPC")) watch_pc_addr_ = uint32_t(strtoul(w, nullptr, 16));
}

Machine::~Machine() = default;

void Machine::log(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
}

std::string Machine::cstr(uint32_t a, size_t max) {
  std::string s;
  for (size_t i = 0; i < max; ++i) {
    char ch = char(read8(a + uint32_t(i)));
    if (!ch) break;
    s += ch;
  }
  return s;
}

// ---- memory allocator ---------------------------------------------------------

void Machine::add_free(uint32_t addr, uint32_t size) { free_list_[addr] = size; }

uint32_t Machine::alloc(uint32_t size, bool chip, bool clear) {
  // Like exec with an A500 trapdoor expansion: the $C00000 RAM has a higher
  // priority than chip RAM, so requests that do not need chip RAM are
  // served from there first. (The "Hostages" release depends on this: part
  // I leaves its buffers allocated, and part II needs the chip RAM.)
  if (!chip)
    if (uint32_t a = alloc_in(size, SLOW_BASE, SLOW_BASE + SLOW_SIZE, clear)) return a;
  return alloc_in(size, 0, CHIP_SIZE, clear);
}

uint32_t Machine::alloc_in(uint32_t size, uint32_t lo, uint32_t hi, bool clear) {
  size = (size + 7) & ~7u;
  if (size == 0) size = 8;
  for (auto it = free_list_.begin(); it != free_list_.end(); ++it) {
    if (it->second < size) continue;
    if (it->first < lo || it->first + size > hi) continue;
    uint32_t a = it->first;
    uint32_t rest = it->second - size;
    free_list_.erase(it);
    if (rest) free_list_[a + size] = rest;
    if (clear)
      for (uint32_t i = 0; i < size; i += 4) write32(a + i, 0);
    hle_->update_memlist();
    return a;
  }
  return 0;
}

void Machine::free(uint32_t addr, uint32_t size) {
  if (!addr) return;
  addr &= ~7u;
  size = (size + 7) & ~7u;
  if (size == 0) size = 8;
  auto [it, ok] = free_list_.emplace(addr, size);
  if (!ok) { log("FreeMem: double free %06x\n", addr); return; }
  auto next = std::next(it);
  if (next != free_list_.end() && it->first + it->second == next->first) {
    it->second += next->second;
    free_list_.erase(next);
  }
  if (it != free_list_.begin()) {
    auto prev = std::prev(it);
    if (prev->first + prev->second == it->first) {
      prev->second += it->second;
      free_list_.erase(it);
    }
  }
  hle_->update_memlist();
}

uint32_t Machine::avail(bool chip, bool largest, bool fast) const {
  uint32_t total = 0;
  for (auto& [a, s] : free_list_) {
    if (chip && a >= CHIP_SIZE) continue;
    if (fast && a < CHIP_SIZE) continue;
    total = largest ? std::max(total, s) : total + s;
  }
  return total;
}

// ---- boot ------------------------------------------------------------------

bool Machine::boot(const std::string& adf_path, const std::string& save_path) {
  if (!disk.load(adf_path, save_path)) return false;
  const auto& img = disk.image();
  if (std::memcmp(img.data(), "DOS", 3) != 0) {
    log("%s: not a bootable disk\n", adf_path.c_str());
    return false;
  }
  if (!coverage_path.empty()) executed_.assign(CHIP_SIZE, 0);
  hle_->boot();
  return true;
}

namespace {
enum class PatchKind { Protection, AccessCodes };

struct Patch {
  PatchKind kind;
  const char* program;
  uint32_t offset;           // in the code hunk
  std::vector<uint16_t> old, now;
  const char* what;
};

// Patches for the "Hostage" release (trackloader disk). The "Hostages"
// crack (AmigaDOS disk) has neither check: its programs do not match.
const Patch patches[] = {
    // Manual word check: the wrong-answer path clears the "correct" flag
    // (clr.b -2(a5)); without it every answer, even an empty one, passes.
    {PatchKind::Protection, "apro1.", 0x6958, {0x422D, 0xFFFE}, {0x4E71, 0x4E71},
     "manual protection"},
    // Rank access codes (HPETS for Captain, EDUAB for Commander): the input
    // loop runs while fewer than 5 letters were typed ("bcc.w check" at
    // $3F9C becomes "bra.w check"), then the code is compared with strcmp
    // and "bne error" ($41AC) shows ERROR. Patched, the prompt is answered
    // "OK" right away.
    {PatchKind::AccessCodes, "apro1.", 0x3F9C, {0x6400}, {0x6000}, "rank access code prompt"},
    {PatchKind::AccessCodes, "apro1.", 0x41AC, {0x6638}, {0x4E71}, "rank access code check"},
};
}  // namespace

void Machine::program_loaded(const std::string& name, const std::vector<uint32_t>& addrs,
                             const std::vector<uint32_t>& sizes) {
  if (addrs.size() > 1) program_a4_ = addrs[1] + 0x7FFE;
  if (verbose) {
    log("[program] %s:", name.c_str());
    for (size_t i = 0; i < addrs.size(); ++i) log(" %06x+%u", addrs[i], sizes[i]);
    log("\n");
  }
  if (addrs.empty()) return;
  for (const Patch& p : patches) {
    if (name != p.program) continue;
    if (p.kind == PatchKind::Protection && !bypass_protection) continue;
    if (p.kind == PatchKind::AccessCodes && !skip_access_codes) continue;
    uint32_t a = addrs[0] + p.offset;
    bool match = p.offset + 2 * p.old.size() <= sizes[0];
    for (size_t i = 0; match && i < p.old.size(); ++i) match = read16(a + 2 * uint32_t(i)) == p.old[i];
    if (!match) { log("%s: cannot apply patch (%s)\n", name.c_str(), p.what); continue; }
    for (size_t i = 0; i < p.now.size(); ++i) write16(a + 2 * uint32_t(i), p.now[i]);
  }
}

void Machine::write_coverage() {
  if (coverage_path.empty() || executed_.empty()) return;
  FILE* f = fopen(coverage_path.c_str(), "w");
  if (!f) return;
  for (uint32_t a = 0; a < executed_.size(); ++a)
    if (executed_[a]) fprintf(f, "x %06x\n", a);
  fclose(f);
}

// ---- 68k <-> host calls ----------------------------------------------------

bool Machine::line_a(uint16_t op) {
  int lib = (op >> 8) & 0xF;
  int idx = op & 0xFF;
  if (lib == HOST_LIB) {
    switch (idx) {
      case TRAP_RETURN:
        if (call_depth_ == 0) {
          hle_->program_exit();
          return true;
        }
        throw ReturnFromCall{};
      case TRAP_BOOT_DONE:
        hle_->boot_done();
        return true;
      case TRAP_RUN_PROGRAM:
        hle_->run_program();
        return true;
      default:
        if (idx >= TRAP_IRQ && idx < TRAP_IRQ + 6) {
          hle_->irq(idx - TRAP_IRQ + 1);
          return true;
        }
        return false;
    }
  }
  hle_->call(lib, idx * 6);
  return true;
}

uint32_t Machine::call68k(uint32_t addr, const uint32_t* d, const uint32_t* a) {
  m68k::Cpu::Regs saved = cpu.save();
  if (d) for (int i = 0; i < 8; ++i) cpu.d[i] = d[i];
  if (a) for (int i = 0; i < 7; ++i) cpu.a[i] = a[i];
  // Leave some room below the interrupted code's stack.
  cpu.a[7] = (saved.a[7] - 64) & ~1u;
  cpu.push32(hle_->ret_stub);
  cpu.pc = addr;
  cpu.stopped = false;
  ++call_depth_;
  try {
    for (;;) {
      execute(line_start_ + line_cycles_);
      if (cpu.cycles >= line_start_ + line_cycles_) run_line();
    }
  } catch (ReturnFromCall&) {
  } catch (...) {
    --call_depth_;
    throw;
  }
  --call_depth_;
  uint32_t ret = cpu.d[0];
  last_call_z_ = cpu.fz;
  cpu.restore(saved);
  return ret;
}

// ---- scheduler -------------------------------------------------------------

void Machine::sync_cias() {
  int64_t elapsed = cpu.cycles - cia_sync_;
  if (elapsed <= 0) return;
  cia_sync_ = cpu.cycles;
  // The CIAs run in real (A500) time whatever the CPU speed setting is.
  eclk_frac_ += double(elapsed) * ECLOCK_PER_LINE / double(line_cycles_);
  uint32_t n = uint32_t(eclk_frac_);
  if (!n) return;
  eclk_frac_ -= n;
  if (cia_[0].tick(n)) raise(INT_PORTS);
  if (cia_[1].tick(n)) raise(INT_EXTER);
}

void Machine::keyboard_line() {
  if (key_hold_lines_ > 0) { --key_hold_lines_; return; }
  if (key_queue_.empty()) return;
  uint8_t code = key_queue_.front();
  key_queue_.pop_front();
  // The keyboard sends the code rotated left by one bit and inverted.
  cia_[0].sdr = uint8_t(~((code << 1) | (code >> 7)));
  if (cia_[0].set_flag(Cia::ICR_SP)) raise(INT_PORTS);
  // Keep each code in SDR for ~1.5 frames: the game polls it once a frame.
  key_hold_lines_ = LINES_PER_FRAME * 3 / 2;
}

void Machine::run_line() {
  // Called when the CPU has consumed a scanline worth of cycles.
  sync_cias();
  blit_sync();
  line_start_ += line_cycles_;
  audio_line();
  if (cia_[1].tod_pulse()) raise(INT_EXTER);  // CIA-B TOD counts HSYNC
  keyboard_line();
  ++vpos_;
  if (vpos_ >= LINES_PER_FRAME) {
    vpos_ = 0;
    end_of_frame();
    raise(INT_VERTB);
    if (cia_[0].tod_pulse()) raise(INT_PORTS);  // CIA-A TOD counts VSYNC
  }
  check_interrupts();
}

void Machine::idle_lines(int n) {
  for (int i = 0; i < n; ++i) {
    cpu.cycles = line_start_ + line_cycles_;
    run_line();
  }
}

void Machine::end_of_frame() {
  hle_->before_frame();
  first_bpl_line_ = -1;
  render_frame();
  if (first_bpl_line_ >= 0)
    view_top_ = std::clamp(first_bpl_line_ - FB_VSTART, 0, FB_H - VIEW_H);
  std::copy(fb_.begin() + long(view_top_) * FB_W, fb_.begin() + long(view_top_ + VIEW_H) * FB_W,
            view_.begin());
  ++frame_count_;
  if (ram_hash_every > 0 && frame_count_ % uint64_t(ram_hash_every) == 0) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : chip_) h = (h ^ b) * 1099511628211ull;
    log("[ramhash] frame %llu %016llx\n", (unsigned long long)frame_count_, (unsigned long long)h);
  }
  if (getenv("HOSTAGES_COPDUMP") && frame_count_ == uint64_t(atoi(getenv("HOSTAGES_COPDUMP")))) {
    for (uint32_t a = cs_.cop1lc_; a < cs_.cop1lc_ + 4 * 200; a += 4) {
      log("cop %06x: %04x %04x\n", a, read16(a), read16(a + 2));
      if (read32(a) == 0xFFFFFFFE) break;
    }
  }
  if (const char* w = getenv("HOSTAGES_WATCHB")) {
    // debugging: log changes of a byte, checked once per frame
    static int last = -1;
    int v = read8(uint32_t(strtoul(w, nullptr, 16)));
    if (v != last) log("[watch] frame %llu: %02x\n", (unsigned long long)frame_count_, v);
    last = v;
  }
  if (getenv("HOSTAGES_SPRDUMP") && frame_count_ % 50 == 0) {
    log("[spr] frame %llu dmacon %04x bplcon2 %04x\n", (unsigned long long)frame_count_, cs_.dmacon_, cs_.bplcon2_);
    for (auto& sp : cs_.spr_)
      log("   pt %06x v %d-%d h %d st %d armed %d data %04x %04x\n", sp.pt, sp.vstart, sp.vstop,
          sp.hstart, sp.state, sp.armed, sp.data, sp.datb);
  }
  if (verbose && frame_count_ % 50 == 0)
    log("[frame %llu] pc=%06x diw %04x-%04x ddf %02x-%02x bplcon0 %04x cop1lc %06x\n",
        (unsigned long long)frame_count_, cpu.pc, cs_.diwstrt_, cs_.diwstop_, cs_.ddfstrt_,
        cs_.ddfstop_, cs_.bplcon0_, cs_.cop1lc_);
  if (host_) {
    host_->frame(view_.data());
    host_->audio(audio_buf_.data(), audio_buf_.size() / 2);
  }
  audio_buf_.clear();
}

void Machine::check_interrupts() {
  if (!(cs_.intena_ & INT_INTEN)) return;
  uint16_t pending = cs_.intena_ & cs_.intreq_ & 0x3FFF;
  if (!pending) return;
  int level;
  if (pending & INT_EXTER) level = 6;
  else if (pending & (INT_RBF | INT_DSKSYN)) level = 5;
  else if (pending & (INT_AUD0 | INT_AUD1 | INT_AUD2 | INT_AUD3)) level = 4;
  else if (pending & (INT_COPER | INT_VERTB | INT_BLIT)) level = 3;
  else if (pending & INT_PORTS) level = 2;
  else level = 1;
  if (level > cpu.imask) cpu.interrupt(level);
}

void Machine::execute(int64_t until) {
  while (cpu.cycles < until) {
    if (cpu.stopped) { cpu.cycles = until; return; }
    uint32_t pc = cpu.pc;
    if (!executed_.empty() && pc < CHIP_SIZE) executed_[pc] = 1;
    if (trace)
      log("%06x  %04x  d0=%08x d1=%08x a0=%08x a7=%08x sr=%04x\n", pc, read16(pc), cpu.d[0],
          cpu.d[1], cpu.a[0], cpu.a[7], cpu.sr());
    cpu.step();
    if (watch_pc_addr_) {
      uint8_t v = read8(watch_pc_addr_);
      if (v != watch_pc_val_ && watch_pc_budget_ > 0) {
        --watch_pc_budget_;
        log("[watchpc] %06x: %02x -> %02x at pc=%06x frame %llu line %d\n", watch_pc_addr_,
            watch_pc_val_, v, cpu.op_pc, (unsigned long long)frame_count_, vpos_);
      }
      watch_pc_val_ = v;
    }
  }
}

int Machine::run() {
  try {
    for (;;) {
      execute(line_start_ + line_cycles_);
      if (cpu.cycles >= line_start_ + line_cycles_) run_line();
    }
  } catch (QuitRequested&) {
  }
  disk.flush();
  write_coverage();
  return 0;
}

}  // namespace amiga
