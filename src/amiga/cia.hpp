// MOS 8520 CIA: two 16-bit timers, interrupt control, serial data register
// and the 24-bit TOD counter. Port pins are handled by the Machine.
#pragma once

#include <cstdint>

namespace amiga {

struct Cia {
  enum : uint8_t { ICR_TA = 1, ICR_TB = 2, ICR_ALRM = 4, ICR_SP = 8, ICR_FLG = 16, ICR_IR = 0x80 };

  uint8_t pra = 0xFF, prb = 0xFF, ddra = 0, ddrb = 0;
  uint16_t ta = 0xFFFF, tb = 0xFFFF, ta_latch = 0xFFFF, tb_latch = 0xFFFF;
  uint8_t cra = 0, crb = 0;
  uint8_t icr = 0, icr_mask = 0;
  uint8_t sdr = 0;
  uint32_t tod = 0, tod_alarm = 0, tod_latch = 0;
  bool tod_latched = false, tod_stopped = false;

  // Advance the timers by n E-clock ticks. Returns true if an enabled
  // interrupt became pending (the caller raises INT2/INT6).
  bool tick(uint32_t n);
  // TOD input pulse (VSYNC for CIA-A, HSYNC for CIA-B).
  bool tod_pulse();
  // Set an interrupt source flag; returns true if it is enabled.
  bool set_flag(uint8_t f);

  uint8_t read(int reg);
  // Returns true if writing the register made an interrupt pending.
  bool write(int reg, uint8_t v);
};

}  // namespace amiga
