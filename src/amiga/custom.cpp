// Custom chip emulation: register file, copper + bitplane display, blitter,
// Paula audio DMA, disk DMA, joystick/mouse ports, and the CIA port pins
// (fire buttons, floppy drive control).
#include "amiga/machine.hpp"
#include "amiga/hle.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace amiga {

namespace {
constexpr uint32_t CUSTOM = 0xDFF000;

inline uint32_t rgb12(uint16_t c) {
  uint32_t r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
  return (r * 17) << 16 | (g * 17) << 8 | (b * 17);
}
}  // namespace

// ---- bus dispatch ------------------------------------------------------------

uint8_t Machine::io_read8(uint32_t addr) {
  if ((addr & 0xFFF000) == CUSTOM) {
    uint16_t v = custom_read(addr & 0x1FE);
    return (addr & 1) ? uint8_t(v) : uint8_t(v >> 8);
  }
  if ((addr & 0xFF0000) == 0xBF0000) {
    // CIA-A: A12 low, data on the odd byte; CIA-B: A13 low, even byte.
    int reg = (addr >> 8) & 15;
    if (!(addr & 0x1000) && (addr & 1)) return cia_read(0, reg);
    if (!(addr & 0x2000) && !(addr & 1)) return cia_read(1, reg);
    return 0xFF;
  }
  if (addr < 0x200000) return chip_[addr & (CHIP_SIZE - 1)];  // chip RAM mirror
  if (addr >= SLOW_BASE && addr < SLOW_BASE + SLOW_SIZE) return slow_[addr - SLOW_BASE];
  return 0;
}

uint16_t Machine::io_read16(uint32_t addr) {
  if ((addr & 0xFFF000) == CUSTOM) return custom_read(addr & 0x1FE);
  return uint16_t(io_read8(addr) << 8 | io_read8(addr + 1));
}

void Machine::io_write8(uint32_t addr, uint8_t v) {
  if ((addr & 0xFFF000) == CUSTOM) {
    // Byte writes to custom registers put the byte on both halves.
    custom_write(addr & 0x1FE, uint16_t(v << 8 | v));
    return;
  }
  if ((addr & 0xFF0000) == 0xBF0000) {
    int reg = (addr >> 8) & 15;
    if (!(addr & 0x1000) && (addr & 1)) cia_write(0, reg, v);
    if (!(addr & 0x2000) && !(addr & 1)) cia_write(1, reg, v);
    return;
  }
  if (addr < 0x200000) { chip_[addr & (CHIP_SIZE - 1)] = v; return; }
  if (addr >= SLOW_BASE && addr < SLOW_BASE + SLOW_SIZE) { slow_[addr - SLOW_BASE] = v; return; }
}

void Machine::io_write16(uint32_t addr, uint16_t v) {
  if ((addr & 0xFFF000) == CUSTOM) { custom_write(addr & 0x1FE, v); return; }
  io_write8(addr, uint8_t(v >> 8));
  io_write8(addr + 1, uint8_t(v));
}

// ---- CIAs and floppy drive ------------------------------------------------------

uint8_t Machine::ciaa_pra_in() {
  bool df0 = !(cia_[1].prb & 0x08);  // /SEL0
  uint8_t v = 0x3F;
  if (!lmb_) v |= 0x40;          // /FIR0
  if (!joy_fire_) v |= 0x80;     // /FIR1
  if (df0 && motor_) v &= ~0x20; // /RDY
  if (df0 && cyl_ == 0) v &= ~0x10;  // /TK0
  // /WPRO and /CHNG high: writable disk inserted
  return v;
}

uint8_t Machine::cia_read(int which, int reg) {
  sync_cias();
  Cia& c = cia_[which];
  switch (reg) {
    case 0x0:
      if (which == 0) return uint8_t((c.pra & c.ddra) | (ciaa_pra_in() & ~c.ddra));
      return uint8_t(c.pra | ~c.ddra);  // CIA-B PRA: serial port lines, idle high
    case 0x1: return uint8_t(c.prb | ~c.ddrb);
    case 0x2: return c.ddra;
    case 0x3: return c.ddrb;
  }
  return c.read(reg);
}

void Machine::cia_write(int which, int reg, uint8_t v) {
  sync_cias();
  Cia& c = cia_[which];
  switch (reg) {
    case 0x0: c.pra = v; return;
    case 0x1: {
      uint8_t old = uint8_t(c.prb | ~c.ddrb);
      c.prb = v;
      if (which == 1) ciab_prb_changed(old, uint8_t(v | ~c.ddrb));
      return;
    }
    case 0x2: c.ddra = v; return;
    case 0x3: c.ddrb = v; return;
  }
  if (c.write(reg, v)) raise(which == 0 ? INT_PORTS : INT_EXTER);
  check_interrupts();
}

void Machine::ciab_prb_changed(uint8_t old, uint8_t v) {
  // /MTR /SEL3../SEL0 SIDE DIR /STEP
  if ((old & 0x08) && !(v & 0x08)) motor_ = !(v & 0x80);  // motor latched on select
  side_ = (v & 0x04) ? 0 : 1;
  bool df0 = !(v & 0x08);
  if (df0 && (old & 0x01) && !(v & 0x01)) {
    if (v & 0x02) { if (cyl_ > 0) --cyl_; }
    else if (cyl_ < 81) ++cyl_;
  }
}

void Machine::disk_dma() {
  // Disk DMA read. The game only uses it for its copy protection: track 3
  // of the original disk is a long track that holds, after a $A144 sync
  // mark, more $AA bytes than a normal track can store. An ADF cannot
  // represent that track, so supply what the drive would deliver.
  uint32_t words = cs_.dsklen_ & 0x3FFF;
  bool write = cs_.dsklen_ & 0x4000;
  if (verbose)
    log("[disk] DMA %s %u words at %06x cyl %d side %d sync %04x adkcon %04x\n",
        write ? "write" : "read", words, cs_.dskpt_, cyl_, side_, cs_.dsksync_, cs_.adkcon_);
  if (write) {
    log("disk DMA write ignored\n");
  } else {
    bool sync = cs_.adkcon_ & 0x0400;
    if (!sync || cs_.dsksync_ == 0x4489)
      log("disk DMA read of an AmigaDOS track (cyl %d side %d) is not supported\n", cyl_, side_);
    for (uint32_t i = 0; i < words; ++i) write16(cs_.dskpt_ + 2 * i, 0xAAAA);
  }
  cs_.dskpt_ += words * 2;
  raise(INT_DSKBLK);
  check_interrupts();
}

// ---- registers ---------------------------------------------------------------

uint16_t Machine::custom_read(uint32_t reg) {
  switch (reg) {
    case 0x002:  // DMACONR (blitter is always done)
      return uint16_t((cs_.dmacon_ & 0x07FF) | (cs_.blt_zero_ ? 0x2000 : 0));
    case 0x004:  // VPOSR: PAL OCS Agnus, long frames, V8
      return uint16_t(0x8000 | ((vpos_ >> 8) & 1));
    case 0x006: return vhpos();
    case 0x00A:  // JOY0DAT (mouse counters)
      return uint16_t((mouse_y_ & 0xFF) << 8 | (mouse_x_ & 0xFF));
    case 0x00C: {  // JOY1DAT (joystick)
      uint16_t v = 0;
      if (joy_right_) v |= 0x0002;
      if (joy_left_) v |= 0x0200;
      if (joy_right_ != joy_down_) v |= 0x0001;
      if (joy_left_ != joy_up_) v |= 0x0100;
      return v;
    }
    case 0x010: return cs_.adkcon_;
    case 0x01A: return 0x8000;  // DSKBYTR: byte ready
    case 0x012: case 0x014: return 0;
    case 0x016: {  // POTGOR: right mouse button = DATLY (bit 10), active low
      uint16_t v = 0xFF00;
      if (rmb_) v &= ~0x0400;
      return v;
    }
    case 0x018: return 0x3000;  // SERDATR: TBE, TSRE
    case 0x01C: return cs_.intena_;
    case 0x01E: return cs_.intreq_;
  }
  return 0;
}

uint16_t Machine::vhpos() {
  int h = int((cpu.cycles - line_start_) * CYCLES_PER_LINE / line_cycles_ / 2);
  if (h > 0xE2) h = 0xE2;
  if (h < 0) h = 0;
  return uint16_t((vpos_ & 0xFF) << 8 | h);
}

void Machine::custom_write(uint32_t reg, uint16_t v) {
  cs_.regs_[reg >> 1] = v;
  auto setclr = [](uint16_t& r, uint16_t v) {
    if (v & 0x8000) r |= v & 0x7FFF; else r &= ~v;
  };
  switch (reg) {
    case 0x020: cs_.dskpt_ = (cs_.dskpt_ & 0xFFFF) | uint32_t(v & 0x1F) << 16; return;
    case 0x022: cs_.dskpt_ = (cs_.dskpt_ & 0xFFFF0000) | (v & 0xFFFE); return;
    case 0x024: {
      // DMA starts when DSKLEN is written twice with DMAEN set.
      bool again = (cs_.dsklen_ & 0x8000) && (v & 0x8000);
      cs_.dsklen_ = v;
      if (again && (cs_.dmacon_ & 0x210) == 0x210) disk_dma();
      return;
    }
    case 0x034: cs_.potgo_ = v; return;
    case 0x07E: cs_.dsksync_ = v; return;
    case 0x040: cs_.bltcon0_ = v; return;
    case 0x042: cs_.bltcon1_ = v; return;
    case 0x044: cs_.bltafwm_ = v; return;
    case 0x046: cs_.bltalwm_ = v; return;
    case 0x048: cs_.bltpt_[2] = (cs_.bltpt_[2] & 0xFFFF) | uint32_t(v & 0x1F) << 16; return;
    case 0x04A: cs_.bltpt_[2] = (cs_.bltpt_[2] & 0xFFFF0000) | (v & 0xFFFE); return;
    case 0x04C: cs_.bltpt_[1] = (cs_.bltpt_[1] & 0xFFFF) | uint32_t(v & 0x1F) << 16; return;
    case 0x04E: cs_.bltpt_[1] = (cs_.bltpt_[1] & 0xFFFF0000) | (v & 0xFFFE); return;
    case 0x050: cs_.bltpt_[0] = (cs_.bltpt_[0] & 0xFFFF) | uint32_t(v & 0x1F) << 16; return;
    case 0x052: cs_.bltpt_[0] = (cs_.bltpt_[0] & 0xFFFF0000) | (v & 0xFFFE); return;
    case 0x054: cs_.bltpt_[3] = (cs_.bltpt_[3] & 0xFFFF) | uint32_t(v & 0x1F) << 16; return;
    case 0x056: cs_.bltpt_[3] = (cs_.bltpt_[3] & 0xFFFF0000) | (v & 0xFFFE); return;
    case 0x058: blit(); return;
    case 0x060: cs_.bltmod_[2] = int16_t(v & 0xFFFE); return;
    case 0x062: cs_.bltmod_[1] = int16_t(v & 0xFFFE); return;
    case 0x064: cs_.bltmod_[0] = int16_t(v & 0xFFFE); return;
    case 0x066: cs_.bltmod_[3] = int16_t(v & 0xFFFE); return;
    case 0x070: cs_.bltdat_[2] = v; return;
    case 0x072: cs_.bltdat_[1] = v; return;
    case 0x074: cs_.bltdat_[0] = v; return;
    case 0x080: cs_.cop1lc_ = (cs_.cop1lc_ & 0xFFFF) | uint32_t(v & 0x1F) << 16; return;
    case 0x082: cs_.cop1lc_ = (cs_.cop1lc_ & 0xFFFF0000) | (v & 0xFFFE); return;
    case 0x084: cs_.cop2lc_ = (cs_.cop2lc_ & 0xFFFF) | uint32_t(v & 0x1F) << 16; return;
    case 0x086: cs_.cop2lc_ = (cs_.cop2lc_ & 0xFFFF0000) | (v & 0xFFFE); return;
    case 0x088: cs_.coppc_ = cs_.cop1lc_; cs_.cop_waiting_ = false; cs_.cop_done_ = false; cs_.cop_resume_line_ = 0; return;
    case 0x08A: cs_.coppc_ = cs_.cop2lc_; cs_.cop_waiting_ = false; cs_.cop_done_ = false; cs_.cop_resume_line_ = 0; return;
    case 0x08E: cs_.diwstrt_ = v; return;
    case 0x090: cs_.diwstop_ = v; return;
    case 0x092: cs_.ddfstrt_ = v & 0xFC; return;
    case 0x094: cs_.ddfstop_ = v & 0xFC; return;
    case 0x096: {
      uint16_t old = cs_.dmacon_;
      setclr(cs_.dmacon_, v);
      for (int ch = 0; ch < 4; ++ch) {
        bool was = (old & 0x200) && (old & (1 << ch));
        bool now = (cs_.dmacon_ & 0x200) && (cs_.dmacon_ & (1 << ch));
        if (now && !was) audio_dma_start(ch);
        if (!now && was) cs_.aud_[ch].active = false;
      }
      return;
    }
    case 0x09A: setclr(cs_.intena_, v); check_interrupts(); return;
    case 0x09C: setclr(cs_.intreq_, v); check_interrupts(); return;
    case 0x09E: setclr(cs_.adkcon_, v); return;
    case 0x100: cs_.bplcon0_ = v; return;
    case 0x102: cs_.bplcon1_ = v; return;
    case 0x104: cs_.bplcon2_ = v; return;
    case 0x108: cs_.bpl1mod_ = int16_t(v & 0xFFFE); return;
    case 0x10A: cs_.bpl2mod_ = int16_t(v & 0xFFFE); return;
  }
  if (reg >= 0x0A0 && reg < 0x0E0) {
    int ch = (reg - 0x0A0) >> 4;
    AudioChannel& a = cs_.aud_[ch];
    switch (reg & 0xF) {
      case 0x0: a.lc = (a.lc & 0xFFFF) | uint32_t(v & 0x1F) << 16; break;
      case 0x2: a.lc = (a.lc & 0xFFFF0000) | (v & 0xFFFE); break;
      case 0x4: a.len = v; break;
      case 0x6: a.per = v; break;
      case 0x8: a.vol = v & 0x7F; break;
      case 0xA: a.dat = v; break;
    }
    return;
  }
  if (reg >= 0x0E0 && reg < 0x0F8) {
    int p = (reg - 0x0E0) >> 2;
    if (reg & 2) cs_.bplpt_[p] = (cs_.bplpt_[p] & 0xFFFF0000) | (v & 0xFFFE);
    else cs_.bplpt_[p] = (cs_.bplpt_[p] & 0xFFFF) | uint32_t(v & 0x1F) << 16;
    return;
  }
  if (reg >= 0x120 && reg < 0x140) {
    SpriteState& sp = cs_.spr_[(reg - 0x120) >> 2];
    if (reg & 2) sp.pt = (sp.pt & 0xFFFF0000) | (v & 0xFFFE);
    else sp.pt = (sp.pt & 0xFFFF) | uint32_t(v & 0x1F) << 16;
    return;
  }
  if (reg >= 0x140 && reg < 0x180) {
    int i = (reg - 0x140) >> 3;
    SpriteState& sp = cs_.spr_[i];
    switch (reg & 6) {
      case 0: sprite_set_pos(i, v); break;
      case 2: sprite_set_ctl(i, v); break;
      case 4: sp.data = v; sp.armed = true; break;
      case 6: sp.datb = v; break;
    }
    return;
  }
  if (reg >= 0x180 && reg < 0x1C0) {
    cs_.color_[(reg - 0x180) >> 1] = v & 0xFFF;
    return;
  }
}

// ---- input from host --------------------------------------------------------

void Machine::mouse_move(int dx, int dy) {
  mouse_x_ = (mouse_x_ + dx) & 0xFF;
  mouse_y_ = (mouse_y_ + dy) & 0xFF;
}

void Machine::mouse_button(int button, bool down) {
  if (button == 0) lmb_ = down; else rmb_ = down;
}

void Machine::joy_state(bool up, bool down, bool left, bool right, bool fire) {
  joy_up_ = up; joy_down_ = down; joy_left_ = left; joy_right_ = right;
  joy_fire_ = fire;
}

void Machine::key_event(uint8_t rawkey, bool down) {
  if (key_queue_.size() < 32) key_queue_.push_back(uint8_t((rawkey & 0x7F) | (down ? 0 : 0x80)));
}

// ---- copper + display -------------------------------------------------------

void Machine::copper_reset() {
  cs_.coppc_ = cs_.cop1lc_;
  cs_.cop_waiting_ = false;
  cs_.cop_done_ = false;
  cs_.cop_resume_line_ = 0;
}

void Machine::copper_run_until(int vpos, int hpos) {
  if (!(cs_.dmacon_ & 0x200) || !(cs_.dmacon_ & 0x80)) return;
  if (vpos < cs_.cop_resume_line_) return;
  for (int guard = 0; guard < 4096 && !cs_.cop_done_; ++guard) {
    if (cs_.cop_waiting_) {
      int vmask = (cs_.cop_wait_mv_ | 0x80);
      int cv = vpos & 0xFF;
      // Beam position comparison (V7 always compared, V8 never).
      int beam = ((cv & vmask) << 8) | (hpos & cs_.cop_wait_mh_);
      int want = ((cs_.cop_wait_v_ & vmask) << 8) | (cs_.cop_wait_h_ & cs_.cop_wait_mh_);
      if (beam < want) return;
      cs_.cop_waiting_ = false;
      // A wait at the very end of a line releases the copper too late to
      // run the next instruction on the same line (this is what makes
      // "WAIT $FFDF; WAIT $xx01" reach lines >= 256).
      if (cs_.cop_wait_h_ >= 0xDE && hpos >= 0xDE) {
        cs_.cop_resume_line_ = vpos + 1;
        return;
      }
    }
    uint16_t ir1 = read16(cs_.coppc_);
    uint16_t ir2 = read16(cs_.coppc_ + 2);
    cs_.coppc_ += 4;
    if (!(ir1 & 1)) {  // MOVE
      uint32_t reg = ir1 & 0x1FE;
      if (reg < 0x40) { cs_.cop_done_ = true; return; }  // illegal: copper stops
      custom_write(reg, ir2);
      continue;
    }
    if (ir1 == 0xFFFF && ir2 == 0xFFFE) { cs_.cop_done_ = true; return; }
    cs_.cop_wait_v_ = ir1 >> 8;
    cs_.cop_wait_h_ = ir1 & 0xFE;
    cs_.cop_wait_mv_ = (ir2 >> 8) & 0x7F;
    cs_.cop_wait_mh_ = ir2 & 0xFE;
    if (ir2 & 1) {  // SKIP
      int vmask = (cs_.cop_wait_mv_ | 0x80);
      int beam = (((vpos & 0xFF) & vmask) << 8) | (hpos & cs_.cop_wait_mh_);
      int want = ((cs_.cop_wait_v_ & vmask) << 8) | (cs_.cop_wait_h_ & cs_.cop_wait_mh_);
      if (beam >= want) cs_.coppc_ += 4;
      continue;
    }
    cs_.cop_waiting_ = true;
  }
}

void Machine::render_line(int line) {
  uint32_t* out = nullptr;
  int fy = line - FB_VSTART;
  if (fy >= 0 && fy < FB_H) out = &fb_[size_t(fy) * FB_W];

  int vstart = cs_.diwstrt_ >> 8;
  int vstop = (cs_.diwstop_ >> 8) | ((cs_.diwstop_ & 0x8000) ? 0 : 0x100);
  int hstart = cs_.diwstrt_ & 0xFF;
  int hstop = (cs_.diwstop_ & 0xFF) | 0x100;
  int nplanes = (cs_.bplcon0_ >> 12) & 7;
  bool hires = cs_.bplcon0_ & 0x8000;
  bool ham = cs_.bplcon0_ & 0x0800;
  bool dpf = cs_.bplcon0_ & 0x0400;
  if (hires) nplanes = std::min(nplanes, 4);
  bool dma = (cs_.dmacon_ & 0x200) && (cs_.dmacon_ & 0x100);
  bool active = dma && nplanes > 0 && line >= vstart && line < vstop;

  uint32_t bg = rgb12(cs_.color_[0]);
  if (!out) {
    // Still consume bitplane data so pointers advance correctly.
    if (active) {
      int nwords = hires ? (cs_.ddfstop_ - cs_.ddfstrt_) / 4 + 2 : (cs_.ddfstop_ - cs_.ddfstrt_) / 8 + 1;
      for (int p = 0; p < nplanes; ++p)
        cs_.bplpt_[p] += uint32_t(nwords * 2 + ((p & 1) ? cs_.bpl2mod_ : cs_.bpl1mod_));
    }
    return;
  }
  for (int x = 0; x < FB_W; ++x) out[x] = bg;
  uint8_t pf[FB_W] = {};  // playfield priority per pixel (0 = transparent)
  if (active) render_playfield(out, pf, nplanes, hires, ham, dpf, hstart, hstop);
  render_sprites(line, out, pf);
}

void Machine::render_playfield(uint32_t* out, uint8_t* pf, int nplanes, bool hires, bool ham,
                               bool dpf, int hstart, int hstop) {

  int nwords = hires ? (cs_.ddfstop_ - cs_.ddfstrt_) / 4 + 2 : (cs_.ddfstop_ - cs_.ddfstrt_) / 8 + 1;
  if (nwords <= 0 || nwords > 64) nwords = 20;
  static uint8_t pix[64 * 16 + 32];
  int npix = nwords * 16;
  std::memset(pix, 0, sizeof pix);
  int delay1 = cs_.bplcon1_ & 15, delay2 = (cs_.bplcon1_ >> 4) & 15;
  for (int p = 0; p < nplanes; ++p) {
    uint32_t pt = cs_.bplpt_[p];
    int delay = (p & 1) ? delay2 : delay1;
    for (int w = 0; w < nwords; ++w) {
      uint16_t data = read16(pt + uint32_t(w * 2));
      for (int b = 0; b < 16; ++b)
        if (data & (0x8000 >> b)) pix[w * 16 + b + delay] |= uint8_t(1 << p);
    }
    cs_.bplpt_[p] = pt + uint32_t(nwords * 2 + ((p & 1) ? cs_.bpl2mod_ : cs_.bpl1mod_));
  }
  // first displayed pixel position (lores pixels, beam coordinates)
  int x0 = hires ? cs_.ddfstrt_ * 2 + 9 : cs_.ddfstrt_ * 2 + 17;
  uint32_t ham_col = cs_.color_[0];
  int step = hires ? 2 : 1;
  for (int i = 0; i < npix; i += step) {
    int hx = x0 + (hires ? i / 2 : i);
    uint8_t c = pix[i];
    uint32_t rgb;
    if (ham && !hires && nplanes >= 6) {
      int ctl = (c >> 4) & 3, val = c & 15;
      switch (ctl) {
        case 0: ham_col = cs_.color_[val]; break;
        case 1: ham_col = (ham_col & 0xFF0) | uint32_t(val); break;
        case 2: ham_col = (ham_col & 0x0FF) | uint32_t(val) << 8; break;
        case 3: ham_col = (ham_col & 0xF0F) | uint32_t(val) << 4; break;
      }
      rgb = rgb12(uint16_t(ham_col));
    } else if (dpf) {
      int pf1 = (c & 1) | ((c >> 1) & 2) | ((c >> 2) & 4);
      int pf2 = ((c >> 1) & 1) | ((c >> 2) & 2) | ((c >> 3) & 4);
      bool pf2pri = cs_.bplcon2_ & 0x40;
      int idx;
      if (pf1 && pf2) idx = pf2pri ? pf2 + 8 : pf1;
      else if (pf1) idx = pf1;
      else if (pf2) idx = pf2 + 8;
      else idx = 0;
      rgb = rgb12(cs_.color_[idx]);
    } else if (nplanes == 6 && !hires) {  // EHB
      uint16_t col = cs_.color_[c & 31];
      if (c & 32) col = (col >> 1) & 0x777;
      rgb = rgb12(col);
    } else {
      rgb = rgb12(cs_.color_[c & 31]);
    }
    if (hx < hstart || hx >= hstop) continue;
    int fx = hx - FB_HSTART;
    if (fx >= 0 && fx < FB_W) {
      out[fx] = rgb;
      // 1 = playfield 1 (or the single playfield), 2 = playfield 2 only
      if (dpf) {
        bool p1 = (c & 1) | (c & 4) | (c & 16), p2 = (c & 2) | (c & 8) | (c & 32);
        pf[fx] = p1 ? 1 : p2 ? 2 : 0;
      } else {
        pf[fx] = c ? 1 : 0;
      }
    }
  }
}

// ---- sprites --------------------------------------------------------------------

void Machine::sprite_dma_line(int line) {
  // Sprite DMA: control words are fetched on the first line after vertical
  // blank and on each sprite's stop line; data words while it is shown.
  if ((cs_.dmacon_ & 0x220) != 0x220 || line < 0x19) return;
  for (int i = 0; i < 8; ++i) {
    SpriteState& sp = cs_.spr_[i];
    if (line == 0x19 || (sp.state == SpriteState::Active && line == sp.vstop)) {
      uint16_t pos = read16(sp.pt), ctl = read16(sp.pt + 2);
      sp.pt += 4;
      sprite_set_pos(i, pos);
      sprite_set_ctl(i, ctl);
      sp.state = (pos || ctl) ? SpriteState::Waiting : SpriteState::Done;
    }
    if (sp.state == SpriteState::Waiting && line == sp.vstart) sp.state = SpriteState::Active;
    if (sp.state == SpriteState::Active) {
      sp.data = read16(sp.pt);
      sp.datb = read16(sp.pt + 2);
      sp.pt += 4;
      sp.armed = true;
    }
  }
}

void Machine::sprite_set_pos(int i, uint16_t v) {
  SpriteState& sp = cs_.spr_[i];
  sp.vstart = (sp.vstart & 0x100) | (v >> 8);
  sp.hstart = (sp.hstart & 1) | ((v & 0xFF) << 1);
}

void Machine::sprite_set_ctl(int i, uint16_t v) {
  SpriteState& sp = cs_.spr_[i];
  sp.vstop = (v >> 8) | ((v & 2) << 7);
  sp.vstart = (sp.vstart & 0xFF) | ((v & 4) << 6);
  sp.hstart = (sp.hstart & ~1) | (v & 1);
  sp.attached = v & 0x80;
  sp.armed = false;
}

void Machine::render_sprites(int line, uint32_t* out, const uint8_t* pf) {
  int pf1p = cs_.bplcon2_ & 7, pf2p = (cs_.bplcon2_ >> 3) & 7;
  // Lower sprite numbers have priority: draw from 7 down to 0.
  for (int i = 7; i >= 0; --i) {
    SpriteState& sp = cs_.spr_[i];
    if (!sp.armed) continue;
    bool attached = (i & 1) && sp.attached;
    const SpriteState& even = cs_.spr_[i & ~1];
    int pair = i >> 1;
    for (int b = 0; b < 16; ++b) {
      int bit = 15 - b;
      int c = ((sp.data >> bit) & 1) | (((sp.datb >> bit) & 1) << 1);
      int col;
      if (attached) {
        // 15-colour sprite: odd sprite gives the high two bits
        int lo = ((even.data >> bit) & 1) | (((even.datb >> bit) & 1) << 1);
        c = (c << 2) | lo;
        if (!c) continue;
        col = 16 + c;
      } else {
        if (!c) continue;
        col = 16 + pair * 4 + c;
      }
      int fx = sp.hstart + 1 + b - FB_HSTART;
      if (fx < 0 || fx >= FB_W) continue;
      // A sprite pair is in front of a playfield whose priority code is
      // greater than the pair number.
      if (pf[fx] == 1 && pair >= pf1p) continue;
      if (pf[fx] == 2 && pair >= pf2p) continue;
      out[fx] = rgb12(cs_.color_[col]);
    }
    if (attached) i &= ~1;  // the even sprite was drawn with it
  }
  (void)line;
}

void Machine::render_frame() {
  copper_reset();
  for (int line = 0; line < LINES_PER_FRAME; ++line) {
    copper_run_until(line, 0x10);
    sprite_dma_line(line);
    render_line(line);
    copper_run_until(line, 0xE2);
  }
}

// ---- blitter ------------------------------------------------------------------

namespace {
inline uint16_t minterm(uint8_t lf, uint16_t a, uint16_t b, uint16_t c) {
  uint16_t r = 0;
  if (lf & 0x01) r |= ~a & ~b & ~c;
  if (lf & 0x02) r |= ~a & ~b & c;
  if (lf & 0x04) r |= ~a & b & ~c;
  if (lf & 0x08) r |= ~a & b & c;
  if (lf & 0x10) r |= a & ~b & ~c;
  if (lf & 0x20) r |= a & ~b & c;
  if (lf & 0x40) r |= a & b & ~c;
  if (lf & 0x80) r |= a & b & c;
  return r;
}
}  // namespace

void Machine::blit() {
  uint16_t size = cs_.regs_[0x058 >> 1];
  int h = size >> 6;
  int w = size & 0x3F;
  if (h == 0) h = 1024;
  if (w == 0) w = 64;
  if (cs_.bltcon1_ & 1) {
    blit_line();
  } else {
    bool useA = cs_.bltcon0_ & 0x800, useB = cs_.bltcon0_ & 0x400;
    bool useC = cs_.bltcon0_ & 0x200, useD = cs_.bltcon0_ & 0x100;
    uint8_t lf = uint8_t(cs_.bltcon0_);
    int ash = cs_.bltcon0_ >> 12, bsh = cs_.bltcon1_ >> 12;
    bool desc = cs_.bltcon1_ & 2;
    bool fill = cs_.bltcon1_ & 0x18;
    bool efe = cs_.bltcon1_ & 0x10;  // exclusive fill (else inclusive)
    int inc = desc ? -2 : 2;
    uint16_t aold = 0, bold = 0;
    bool zero = true;
    uint32_t& apt = cs_.bltpt_[0];
    uint32_t& bpt = cs_.bltpt_[1];
    uint32_t& cpt = cs_.bltpt_[2];
    uint32_t& dpt = cs_.bltpt_[3];
    for (int y = 0; y < h; ++y) {
      bool fc = cs_.bltcon1_ & 4;  // FCI
      for (int x = 0; x < w; ++x) {
        if (useA) { cs_.bltdat_[0] = read16(apt); apt += uint32_t(inc); }
        if (useB) { cs_.bltdat_[1] = read16(bpt); bpt += uint32_t(inc); }
        if (useC) { cs_.bltdat_[2] = read16(cpt); cpt += uint32_t(inc); }
        uint16_t a = cs_.bltdat_[0];
        if (x == 0) a &= cs_.bltafwm_;
        if (x == w - 1) a &= cs_.bltalwm_;
        uint16_t as, bs;
        if (!desc) {
          as = uint16_t(((uint32_t(aold) << 16) | a) >> ash);
          bs = uint16_t(((uint32_t(bold) << 16) | cs_.bltdat_[1]) >> bsh);
        } else {
          as = uint16_t(((uint32_t(a) << 16) | aold) >> (16 - ash));
          bs = uint16_t(((uint32_t(cs_.bltdat_[1]) << 16) | bold) >> (16 - bsh));
        }
        aold = a;
        bold = cs_.bltdat_[1];
        uint16_t d = minterm(lf, as, bs, cs_.bltdat_[2]);
        if (fill) {
          uint16_t r = 0;
          for (int bit = 0; bit < 16; ++bit) {
            uint16_t m = uint16_t(1 << bit);
            bool v = d & m;
            if (efe) { fc ^= v; if (fc) r |= m; }
            else { if (fc || v) r |= m; fc ^= v; }
          }
          d = r;
        }
        if (d) zero = false;
        if (useD) { write16(dpt, d); dpt += uint32_t(inc); }
      }
      if (useA) apt += uint32_t(desc ? -cs_.bltmod_[0] : cs_.bltmod_[0]);
      if (useB) bpt += uint32_t(desc ? -cs_.bltmod_[1] : cs_.bltmod_[1]);
      if (useC) cpt += uint32_t(desc ? -cs_.bltmod_[2] : cs_.bltmod_[2]);
      if (useD) dpt += uint32_t(desc ? -cs_.bltmod_[3] : cs_.bltmod_[3]);
    }
    cs_.blt_zero_ = zero;
  }
  raise(INT_BLIT);
  // The blit happens instantly; charge a rough DMA cost to the CPU.
  cpu.cycles += int64_t(w) * h * 2;
}

void Machine::blit_line() {
  uint16_t size = cs_.regs_[0x058 >> 1];
  int len = size >> 6;
  if (len == 0) len = 1024;
  uint8_t lf = uint8_t(cs_.bltcon0_);
  int ashift = cs_.bltcon0_ >> 12;
  int bshift = cs_.bltcon1_ >> 12;
  bool sing = cs_.bltcon1_ & 2;
  bool sud = cs_.bltcon1_ & 0x10, sul = cs_.bltcon1_ & 0x08, aul = cs_.bltcon1_ & 0x04;
  bool sign = cs_.bltcon1_ & 0x40;
  int16_t err = int16_t(cs_.bltpt_[0] & 0xFFFF);
  uint32_t cpt = cs_.bltpt_[2];
  uint32_t dpt = cs_.bltpt_[3];
  uint16_t texture = cs_.bltdat_[1];
  bool onedot = false;
  bool zero = true;
  uint16_t ahold = cs_.bltdat_[0] & cs_.bltafwm_;

  auto incx = [&]() { if (++ashift == 16) { ashift = 0; cpt += 2; } };
  auto decx = [&]() { if (ashift-- == 0) { ashift = 15; cpt -= 2; } };
  auto incy = [&]() { cpt += uint32_t(cs_.bltmod_[2]); onedot = false; };
  auto decy = [&]() { cpt -= uint32_t(cs_.bltmod_[2]); onedot = false; };

  for (int i = 0; i < len; ++i) {
    uint16_t c = (cs_.bltcon0_ & 0x200) ? read16(cpt) : cs_.bltdat_[2];
    uint16_t a = uint16_t(ahold >> ashift);
    bool draw = !(sing && onedot);
    uint16_t b = ((texture >> bshift) & 1) ? 0xFFFF : 0;
    if (!draw) a = 0;
    uint16_t d = minterm(lf, a, b, c);
    onedot = true;
    if (d) zero = false;
    uint32_t write_at = dpt;
    // step
    if (!sign) err = int16_t(err + cs_.bltmod_[0]);
    else err = int16_t(err + cs_.bltmod_[1]);
    if (!sign) {
      if (sud) { if (sul) decy(); else incy(); }
      else { if (sul) decx(); else incx(); }
    }
    if (sud) { if (aul) decx(); else incx(); }
    else { if (aul) decy(); else incy(); }
    sign = err < 0;
    bshift = (bshift - 1) & 15;
    if ((cs_.bltcon0_ & 0x100) && draw) write16(write_at, d);
    dpt = cpt;
  }
  cs_.bltpt_[0] = (cs_.bltpt_[0] & 0xFFFF0000) | uint16_t(err);
  cs_.bltpt_[2] = cpt;
  cs_.bltpt_[3] = dpt;
  cs_.bltcon0_ = uint16_t((cs_.bltcon0_ & 0x0FFF) | (ashift << 12));
  cs_.bltcon1_ = uint16_t((cs_.bltcon1_ & 0x0FBF) | (bshift << 12) | (sign ? 0x40 : 0));
  cs_.blt_zero_ = zero;
}

// ---- Paula --------------------------------------------------------------------

void Machine::audio_dma_start(int ch) {
  AudioChannel& a = cs_.aud_[ch];
  a.active = true;
  a.pt = a.lc;
  a.remaining = a.len ? a.len : 0x10000;
  a.byte_index = 0;
  a.phase = 0;
  raise(uint16_t(INT_AUD0 << ch));
}

void Machine::audio_advance(double ticks, int16_t* out, int frames) {
  for (int f = 0; f < frames; ++f) {
    int l = 0, r = 0;
    for (int ch = 0; ch < 4; ++ch) {
      AudioChannel& a = cs_.aud_[ch];
      if (a.active) {
        int per = a.per < 124 ? 124 : a.per;
        a.phase += ticks;
        while (a.phase >= per) {
          a.phase -= per;
          uint16_t w = read16(a.pt);
          a.sample = int8_t(a.byte_index ? (w & 0xFF) : (w >> 8));
          if (++a.byte_index == 2) {
            a.byte_index = 0;
            a.pt += 2;
            if (--a.remaining == 0) {
              a.pt = a.lc;
              a.remaining = a.len ? a.len : 0x10000;
              raise(uint16_t(INT_AUD0 << ch));
            }
          }
        }
      }
      int vol = a.vol > 64 ? 64 : a.vol;
      int s = a.active ? a.sample * vol : 0;
      if (ch == 0 || ch == 3) l += s; else r += s;
    }
    // A500 output stage: fixed one-pole RC low-pass at ~4.4 kHz.
    lp_l_ += lp_k_ * (double(l) - lp_l_);
    lp_r_ += lp_k_ * (double(r) - lp_r_);
    // 2 channels × 127 × 64 → scale to 16 bit
    out[f * 2] = int16_t(std::clamp(int(lp_l_ * 2), -32768, 32767));
    out[f * 2 + 1] = int16_t(std::clamp(int(lp_r_ * 2), -32768, 32767));
  }
}

void Machine::audio_line() {
  int rate = host_ ? host_->audio_rate() : 48000;
  lp_k_ = 1.0 - std::exp(-2.0 * 3.14159265358979 * 4400.0 / rate);
  audio_frac_ += double(rate) / (LINES_PER_FRAME * 60.0);
  int n = int(audio_frac_);
  if (n <= 0) return;
  audio_frac_ -= n;
  size_t off = audio_buf_.size();
  audio_buf_.resize(off + size_t(n) * 2);
  audio_advance(PAULA_CLOCK / rate, &audio_buf_[off], n);
}

}  // namespace amiga
