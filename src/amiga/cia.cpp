#include "amiga/cia.hpp"

namespace amiga {

bool Cia::set_flag(uint8_t f) {
  icr |= f;
  if (icr_mask & f) {
    icr |= ICR_IR;
    return true;
  }
  return false;
}

bool Cia::tick(uint32_t n) {
  bool irq = false;
  uint32_t ta_underflows = 0;
  if (cra & 1) {
    // Timer A counts E-clock pulses (INMODE 0).
    uint32_t t = n;
    while (t && (cra & 1)) {
      if (t <= ta) { ta = uint16_t(ta - t); t = 0; break; }
      t -= uint32_t(ta) + 1;
      ta = ta_latch;
      ++ta_underflows;
      irq |= set_flag(ICR_TA);
      if (cra & 8) { cra &= uint8_t(~1); break; }  // one-shot
    }
  }
  if (crb & 1) {
    // INMODE: 0 = E clock, 2 = timer A underflows.
    uint32_t t = (crb & 0x60) == 0x40 ? ta_underflows : (crb & 0x60) == 0 ? n : 0;
    while (t && (crb & 1)) {
      if (t <= tb) { tb = uint16_t(tb - t); break; }
      t -= uint32_t(tb) + 1;
      tb = tb_latch;
      irq |= set_flag(ICR_TB);
      if (crb & 8) { crb &= uint8_t(~1); break; }
    }
  }
  return irq;
}

bool Cia::tod_pulse() {
  if (tod_stopped) return false;
  tod = (tod + 1) & 0xFFFFFF;
  if (tod == tod_alarm) return set_flag(ICR_ALRM);
  return false;
}

uint8_t Cia::read(int reg) {
  switch (reg) {
    case 0x4: return uint8_t(ta);
    case 0x5: return uint8_t(ta >> 8);
    case 0x6: return uint8_t(tb);
    case 0x7: return uint8_t(tb >> 8);
    case 0x8: {
      uint32_t v = tod_latched ? tod_latch : tod;
      tod_latched = false;
      return uint8_t(v);
    }
    case 0x9: return uint8_t((tod_latched ? tod_latch : tod) >> 8);
    case 0xA:
      tod_latch = tod;
      tod_latched = true;
      return uint8_t(tod >> 16);
    case 0xC: return sdr;
    case 0xD: {
      uint8_t v = icr;
      icr = 0;
      return v;
    }
    case 0xE: return cra;
    case 0xF: return crb;
  }
  return 0xFF;
}

bool Cia::write(int reg, uint8_t v) {
  switch (reg) {
    case 0x4: ta_latch = uint16_t((ta_latch & 0xFF00) | v); break;
    case 0x5:
      ta_latch = uint16_t((ta_latch & 0x00FF) | v << 8);
      // Writing the high byte loads the counter if the timer is stopped,
      // and starts it in one-shot mode.
      if (!(cra & 1)) ta = ta_latch;
      if (cra & 8) { ta = ta_latch; cra |= 1; }
      break;
    case 0x6: tb_latch = uint16_t((tb_latch & 0xFF00) | v); break;
    case 0x7:
      tb_latch = uint16_t((tb_latch & 0x00FF) | v << 8);
      if (!(crb & 1)) tb = tb_latch;
      if (crb & 8) { tb = tb_latch; crb |= 1; }
      break;
    case 0x8:
    case 0x9:
    case 0xA: {
      int shift = (reg - 8) * 8;
      if (crb & 0x80) {
        tod_alarm = (tod_alarm & ~(0xFFu << shift)) | uint32_t(v) << shift;
      } else {
        tod = (tod & ~(0xFFu << shift)) | uint32_t(v) << shift;
        tod_stopped = reg == 0xA;   // writing the high byte stops TOD
        if (reg == 0x8) tod_stopped = false;
      }
      break;
    }
    case 0xC: sdr = v; break;
    case 0xD:
      if (v & 0x80) icr_mask |= v & 0x1F;
      else icr_mask &= uint8_t(~v);
      if (icr & icr_mask & 0x1F) {
        icr |= ICR_IR;
        return true;
      }
      break;
    case 0xE:
      if (v & 0x10) ta = ta_latch;  // force load
      cra = v & uint8_t(~0x10);
      break;
    case 0xF:
      if (v & 0x10) tb = tb_latch;
      crb = v & uint8_t(~0x10);
      break;
  }
  return false;
}

}  // namespace amiga
