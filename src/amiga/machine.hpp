// Amiga 500 machine model for running the original Hostages code:
// 68000 core + custom chips (copper/display, blitter, Paula, disk DMA) +
// the two 8520 CIAs + high-level emulation of exec and trackdisk.device.
#pragma once

#include "amiga/cia.hpp"
#include "amiga/disk.hpp"
#include "m68k/cpu.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace amiga {

// 512 KB chip RAM (mirrored up to $1FFFFF) + 512 KB slow RAM at $C00000,
// like a stock A500 with a trapdoor expansion.
constexpr uint32_t CHIP_SIZE = 512 * 1024;
constexpr uint32_t SLOW_BASE = 0xC00000;
constexpr uint32_t SLOW_SIZE = 512 * 1024;

// PAL timing
constexpr int LINES_PER_FRAME = 313;
constexpr int CYCLES_PER_LINE = 454;  // CPU clocks (227 colour clocks × 2)
constexpr double CPU_HZ = 7093790.0;
constexpr double PAULA_CLOCK = 3546895.0;
constexpr double FRAME_HZ = CPU_HZ / (CYCLES_PER_LINE * LINES_PER_FRAME);  // 49.92
constexpr double ECLOCK_PER_LINE = CYCLES_PER_LINE / 10.0;

// Rendered frame: lores pixels of the PAL display area from $2C81.
constexpr int FB_W = 320;
constexpr int FB_H = 256;
// The game shows Atari-ST style 320×200 screens; the host gets a 200-line
// view that follows the first displayed bitplane line (the "Hostage"
// release uses line $2C, the Intuition screen of "Hostages" $3B).
constexpr int VIEW_H = 200;
constexpr int FB_HSTART = 0x81;  // lores pixel position of fb x=0
constexpr int FB_VSTART = 0x2C;  // beam line of fb y=0

struct QuitRequested {};

// INTENA/INTREQ bits
enum : uint16_t {
  INT_TBE = 1 << 0, INT_DSKBLK = 1 << 1, INT_SOFT = 1 << 2, INT_PORTS = 1 << 3,
  INT_COPER = 1 << 4, INT_VERTB = 1 << 5, INT_BLIT = 1 << 6,
  INT_AUD0 = 1 << 7, INT_AUD1 = 1 << 8, INT_AUD2 = 1 << 9, INT_AUD3 = 1 << 10,
  INT_RBF = 1 << 11, INT_DSKSYN = 1 << 12, INT_EXTER = 1 << 13, INT_INTEN = 1 << 14,
};

struct BlitState {
  int w = 0, h = 0, y = 0;  // size in words/rows, rows done
  bool line = false;
  int64_t start = 0;        // cpu.cycles when the first row starts
  int64_t row_cycles = 1;   // CPU clocks per row
  uint16_t aold = 0, bold = 0;  // previous A/B words (barrel shifter)
};

struct SpriteState {
  enum State : uint8_t { Done, Waiting, Active };
  uint32_t pt = 0;
  uint16_t data = 0, datb = 0;
  int vstart = 0, vstop = 0, hstart = 0;
  bool attached = false, armed = false;
  State state = Done;
};

struct AudioChannel {
  uint32_t lc = 0;      // AUDxLC (reload)
  uint16_t len = 0;     // AUDxLEN (reload, words)
  uint16_t per = 0;
  uint16_t vol = 0;
  uint16_t dat = 0;
  // DMA state
  bool active = false;
  uint32_t pt = 0;
  uint32_t remaining = 0;  // words left in current block
  int byte_index = 0;      // 0/1 within the current word
  int8_t sample = 0;
  double phase = 0;        // fractional Paula ticks
};

// Host side of the machine (SDL front end).
struct Host {
  virtual ~Host() = default;
  // Called once per emulated frame with the FB_W×VIEW_H RGB888 view.
  virtual void frame(const uint32_t* fb) = 0;
  // Audio samples (interleaved stereo, signed 16-bit) produced this frame.
  virtual void audio(const int16_t* samples, size_t frames) = 0;
  virtual int audio_rate() const { return 48000; }
};

class Machine : public m68k::Bus {
public:
  Machine();
  ~Machine() override;

  // ---- setup ------------------------------------------------------------
  // Insert the game disk (ADF) and prepare the boot: the bootblock is run
  // exactly as Kickstart's strap module would run it. Disk writes go to
  // save_path (which is used instead of the ADF once it exists).
  bool boot(const std::string& adf_path, const std::string& save_path);
  void set_host(Host* h) { host_ = h; }
  // Run until QuitRequested is thrown by the host.
  int run();
  // Advance exactly one PAL frame (or present while debug-paused).
  // Returns false when the machine has quit.
  bool step_frame();

  // ---- memory helpers for HLE code -------------------------------------
  uint8_t r8(uint32_t a) { return read8(a); }
  uint16_t r16(uint32_t a) { return read16(a); }
  uint32_t r32(uint32_t a) { return read32(a); }
  void w8(uint32_t a, uint8_t v) { write8(a, v); }
  void w16(uint32_t a, uint16_t v) { write16(a, v); }
  void w32(uint32_t a, uint32_t v) { write32(a, v); }
  std::string cstr(uint32_t a, size_t max = 256);

  // Memory allocator (first fit, chip or slow RAM like exec's MemHeaders).
  uint32_t alloc(uint32_t size, bool chip, bool clear = true);
  // Allocate within [lo, hi).
  uint32_t alloc_in(uint32_t size, uint32_t lo, uint32_t hi, bool clear = true);
  void free(uint32_t addr, uint32_t size);
  uint32_t avail(bool chip, bool largest, bool fast = false) const;
  void add_free(uint32_t addr, uint32_t size);

  // Call 68k code at `addr` with the given registers (a7 is managed).
  // Returns d0; the Z flag of the result is in last_call_z().
  uint32_t call68k(uint32_t addr, const uint32_t* d = nullptr, const uint32_t* a = nullptr);
  bool last_call_z() const { return last_call_z_; }

  // Advance emulated time without executing CPU code.
  void idle_lines(int n);

  // ---- input (called by host) -------------------------------------------
  void key_event(uint8_t rawkey, bool down);
  void mouse_move(int dx, int dy);
  void mouse_button(int button, bool down);  // 0 = left, 1 = right
  void joy_state(bool up, bool down, bool left, bool right, bool fire);

  // ---- bus ----------------------------------------------------------------
  uint8_t io_read8(uint32_t addr) override;
  uint16_t io_read16(uint32_t addr) override;
  void io_write8(uint32_t addr, uint8_t v) override;
  void io_write16(uint32_t addr, uint16_t v) override;

  m68k::Cpu cpu;
  bool trace = false;
  bool verbose = false;
  bool blitlog_ = getenv("HOSTAGES_BLITLOG") != nullptr;  // debugging
  bool audlog_ = getenv("HOSTAGES_AUDLOG") != nullptr;    // debugging
  uint64_t audlog_from_ = audlog_ ? strtoull(getenv("HOSTAGES_AUDLOG"), nullptr, 10) : 0;
  Disk disk;

  // CPU speed multiplier: the CPU gets cpu_speed × 454 cycles per scanline.
  void set_cpu_speed(double s) { line_cycles_ = int64_t(CYCLES_PER_LINE * (s < 0.1 ? 0.1 : s)); }

  // Called by the HLE when stage 2 starts a program it has loaded.
  void program_loaded(const std::string& name, const std::vector<uint32_t>& addrs,
                      const std::vector<uint32_t>& sizes);
  // Small-data base (a4) of the program started last: its data hunk + $7FFE
  // (both programs are compiled with the small data model).
  uint32_t program_a4() const { return program_a4_; }
  // Name of the program started last ("apro1.", "it.", …); empty before any load.
  const std::string& program_name() const { return program_name_; }
  // Bypass the manual word check (on by default, --protection keeps it).
  bool bypass_protection = true;
  // Accept any rank access code (Captain/Commander) without knowing it.
  bool skip_access_codes = false;

  // ---- debug / save states ----------------------------------------------
  // When true the scheduler still presents frames but does not advance the
  // CPU (used by the host debug menu).
  bool debug_pause = false;
  // Snapshot the whole machine (RAM, CPU, chips, CIAs, disk, HLE) to path.
  bool save_state(const std::string& path) const;
  bool load_state(const std::string& path);

  // Lightweight view of a HOSTSAV1 file for UI labels (does not touch the live machine).
  struct SaveSummary {
    bool ok = false;
    std::string program_name;
    uint32_t program_a4 = 0;
    std::vector<uint8_t> chip;
    std::vector<uint8_t> slow;
    uint8_t read8(uint32_t a) const {
      a &= 0xFFFFFF;
      if (a < chip.size()) return chip[size_t(a)];
      if (a >= SLOW_BASE && (a - SLOW_BASE) < slow.size()) return slow[size_t(a - SLOW_BASE)];
      return 0;
    }
    uint16_t read16(uint32_t a) const {
      return uint16_t(uint16_t(read8(a)) << 8 | read8(a + 1));
    }
  };
  static bool summarize_save(const std::string& path, SaveSummary& out);
  // Soft-reset: re-run boot() with the same ADF/save paths.
  bool soft_reset();
  // Force stage-I marksmen into target positions so Return advances to part II
  // (offsets differ between the trackloader and AmigaDOS releases).
  void complete_stage1();
  // Paths remembered from the last successful boot() (for soft_reset).
  const std::string& adf_path() const { return adf_path_; }
  const std::string& disk_save_path() const { return disk_save_path_; }

  // Coverage: record executed instruction addresses (chip RAM).
  std::string coverage_path;
  void write_coverage();

  int ram_hash_every = 0;       // log a hash of chip RAM every N frames
  uint64_t frame_count() const { return frame_count_; }

  void log(const char* fmt, ...) const;

private:
  friend struct Hle;
  // scheduler
  void execute(int64_t until);
  void run_line();
  void end_of_frame();
  void check_interrupts();
  void raise(uint16_t bits) { cs_.intreq_ |= bits; }

  // custom chips
  uint16_t custom_read(uint32_t reg);
  void custom_write(uint32_t reg, uint16_t v);
  uint16_t vhpos();
  void blit();
  void blit_sync();    // run the rows of the active blit that are due
  void blit_finish();  // complete the active blit now
  void blit_row();
  void blit_line();
  void render_frame();
  void copper_reset();
  void copper_run_until(int vpos, int hpos);
  void render_line(int line);
  void render_playfield(uint32_t* out, uint8_t* pf, int nplanes, bool hires, bool ham, bool dpf,
                        int hstart, int hstop);
  void sprite_dma_line(int line);
  void sprite_set_pos(int i, uint16_t v);
  void sprite_set_ctl(int i, uint16_t v);
  void render_sprites(int line, uint32_t* out, const uint8_t* pf);
  void audio_dma_start(int ch);
  void audio_advance(double paula_ticks, int16_t* out, int frames);
  void audio_line();
  void disk_dma();

  // CIAs
  void sync_cias();
  uint8_t cia_read(int which, int reg);
  void cia_write(int which, int reg, uint8_t v);
  uint8_t ciaa_pra_in();
  void ciab_prb_changed(uint8_t old, uint8_t v);
  void keyboard_line();

  bool line_a(uint16_t op);

  std::vector<uint8_t> chip_, slow_;
  Host* host_ = nullptr;
  std::unique_ptr<struct Hle> hle_;

  // time
  int64_t line_start_ = 0;   // cpu.cycles at start of current line
  int64_t line_cycles_ = CYCLES_PER_LINE;
  int64_t cia_sync_ = 0;     // cpu.cycles up to which the CIAs have run
  double eclk_frac_ = 0;
  int vpos_ = 0;
  uint64_t frame_count_ = 0;
  int call_depth_ = 0;
  bool last_call_z_ = false;

  struct CustomState {
    uint16_t regs_[256] = {};
    uint16_t dmacon_ = 0, intena_ = 0, intreq_ = 0;
    uint16_t adkcon_ = 0;
    uint32_t cop1lc_ = 0, cop2lc_ = 0;
    uint32_t bplpt_[6] = {};
    uint16_t color_[32] = {};
    uint16_t bplcon0_ = 0, bplcon1_ = 0, bplcon2_ = 0;
    int16_t bpl1mod_ = 0, bpl2mod_ = 0;
    uint16_t diwstrt_ = 0x2C81, diwstop_ = 0x2CC1, ddfstrt_ = 0x38, ddfstop_ = 0xD0;
    // copper
    uint32_t coppc_ = 0;
    bool cop_waiting_ = false;
    bool cop_done_ = false;
    int cop_resume_line_ = 0;
    uint16_t cop_wait_v_ = 0, cop_wait_h_ = 0, cop_wait_mv_ = 0, cop_wait_mh_ = 0;
    // blitter
    uint16_t bltcon0_ = 0, bltcon1_ = 0, bltafwm_ = 0xFFFF, bltalwm_ = 0xFFFF;
    uint32_t bltpt_[4] = {};  // A B C D
    int16_t bltmod_[4] = {};
    uint16_t bltdat_[3] = {};  // A B C
    bool blt_zero_ = true;
    bool blt_active_ = false;
    BlitState blit_;
    uint16_t potgo_ = 0;
    // disk
    uint32_t dskpt_ = 0;
    uint16_t dsklen_ = 0, dsksync_ = 0x4489;
    AudioChannel aud_[4];
    SpriteState spr_[8];
  } cs_;

  Cia cia_[2];          // 0 = CIA-A ($BFE001), 1 = CIA-B ($BFD000)
  // floppy drive DF0
  int cyl_ = 0;
  int side_ = 0;
  bool motor_ = false;
  // keyboard: raw key events waiting to be shifted into CIA-A SDR
  std::deque<uint8_t> key_queue_;
  int key_hold_lines_ = 0;

  // display
  std::vector<uint32_t> fb_;
  std::vector<uint32_t> view_;
  int first_bpl_line_ = -1;  // first line with bitplanes in the current frame
  int view_top_ = 0;         // fb row shown at the top of the view
  // audio
  std::vector<int16_t> audio_buf_;
  double audio_frac_ = 0;
  double lp_l_ = 0, lp_r_ = 0, lp_k_ = 0.4;
  // input
  int mouse_x_ = 0, mouse_y_ = 0;
  bool lmb_ = false, rmb_ = false;
  bool joy_up_ = false, joy_down_ = false, joy_left_ = false, joy_right_ = false, joy_fire_ = false;

  // heap: free blocks per region (addr → size)
  std::map<uint32_t, uint32_t> free_list_;

  std::vector<uint8_t> executed_;
  uint32_t program_a4_ = 0;
  std::string program_name_;
  std::string adf_path_;
  std::string disk_save_path_;
  uint32_t watch_pc_addr_ = 0;
  uint8_t watch_pc_val_ = 0;
  int watch_pc_budget_ = 40;
};

}  // namespace amiga
