// High-level emulation of the parts of Kickstart the game relies on:
// exec (memory, lists, ports, interrupts, devices), trackdisk.device on the
// ADF, minimal dos/graphics bases, and the boot strap that runs the
// bootblock.
#pragma once

#include "amiga/machine.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace amiga {

// Line-A opcode $A<lib><idx>: lib 15 is reserved for host traps.
constexpr int HOST_LIB = 15;
enum {
  TRAP_RETURN = 0,     // end of a call68k() callback
  TRAP_BOOT_DONE = 1,  // bootblock returned to the strap
  TRAP_IRQ = 2,        // TRAP_IRQ + level - 1: autovector levels 1-6
  TRAP_RUN_PROGRAM = 8,  // stage 2 jumps into a program it has loaded
};

struct ReturnFromCall {};

enum Lib { LIB_EXEC = 0, LIB_DOS, LIB_GFX, LIB_TRACKDISK, LIB_INTUITION, LIB_MATHFFP, LIB_COUNT };

struct Hle {
  explicit Hle(Machine& m) : m(m) {}

  // Set up exec and the low-memory vectors, read the bootblock and prepare
  // the CPU to run it with an open trackdisk IORequest (like strap).
  void boot();
  void boot_done();
  void run_program();
  void call(int lib, int lvo);
  void irq(int level);
  // Called before each frame is rendered.
  void before_frame();
  void keyboard_irq();
  // Mirror the allocator's free blocks into exec's MemList (MemHeaders
  // with MemChunk lists), for programs that inspect it.
  void update_memlist();
  // The running program returned (or called Exit): continue the script.
  void program_exit();

  uint32_t ret_stub = 0;
  uint32_t heap_start = 0;
  uint32_t stage2_ = 0;  // code hunk of the stage-2 loader ("tatou.")

private:
  Machine& m;
  uint32_t& D(int i) { return m.cpu.d[i]; }
  uint32_t& A(int i) { return m.cpu.a[i]; }

  uint32_t low_alloc(uint32_t size);
  uint32_t make_library(int lib, const char* name, int nvec, uint32_t possize, uint8_t type);
  uint32_t make_string(const char* s);
  void unknown(int lib, int lvo);

  void exec(int lvo);
  void dos(int lvo);
  void gfx(int lvo);
  void trackdisk(int lvo);
  void intuition(int lvo);
  void mathffp(int lvo);

  // AmigaDOS boot (disks with a file system, e.g. the "Hostages" release):
  // run the program that s/startup-sequence starts, as a CLI process.
  void boot_dos();
  // Run the next program of the startup-sequence (after the previous one
  // returned). Returns false if there is none.
  bool next_command();
  std::vector<std::string> script_;
  size_t script_pos_ = 0;
  uint32_t stack_size_ = 4000;
  uint32_t seglist_ = 0;
  uint32_t cli_ = 0;

  // Intuition screens: the display is built from the screen's BitMap and
  // ColorMap as a copper list (rebuilt when colours or planes change).
  struct Screen {
    uint32_t addr = 0;       // struct Screen
    uint32_t copper = 0;     // our copper list
    uint32_t colors = 0;     // its first colour move
    bool own_bitmap = false;
  };
  std::vector<Screen> screens_;
  uint32_t view_ = 0;     // Intuition's View (GfxBase->ActiView)
  uint32_t cprlist_ = 0;  // its LOFCprList
  uint32_t copinit_ = 0;  // GfxBase->copinit: jumps to the View's list
  uint32_t open_screen(uint32_t newscreen);
  void build_screen_copper(Screen& s);
  void rebuild_viewport(uint32_t vp);
  void update_screen_colors(uint32_t vp);
  uint32_t blank_sprite_ = 0;
  void show_front_screen();
  // LoadSeg from the disk's file system; returns the segment list BPTR (0
  // on failure) and the entry point.
  uint32_t load_seg(const std::string& path, uint32_t* entry = nullptr);
  void unload_seg(uint32_t seglist);
  uint32_t make_lock(int block);
  int lock_block(uint32_t lock);  // -1 if not a lock of ours
  struct DosFile {
    std::vector<uint8_t> data;
    uint32_t pos = 0;
    bool write = false;
    std::string name;
  };
  std::map<uint32_t, DosFile> files_;  // FileHandle BPTR → file
  std::map<uint32_t, int> locks_;      // FileLock BPTR → header block
  std::map<uint32_t, int> exnext_;     // FileInfoBlock → next index for ExNext
  uint32_t ioerr_ = 0;
  uint32_t current_dir_ = 0;
  uint64_t delay_until_ = 0;
  uint64_t wait_frame_ = 0;
  void plot(uint32_t rp, int x, int y);

  // exec helpers
  void new_list(uint32_t list);
  void add_tail(uint32_t list, uint32_t node);
  void add_head(uint32_t list, uint32_t node);
  uint32_t rem_head(uint32_t list);
  uint32_t rem_tail(uint32_t list);
  void remove(uint32_t node);
  void enqueue(uint32_t list, uint32_t node);
  void insert(uint32_t list, uint32_t node, uint32_t pred);
  uint32_t find_name(uint32_t list, const std::string& name);
  void put_msg(uint32_t port, uint32_t msg);
  void reply_msg(uint32_t msg);
  uint32_t wait(uint32_t sigs);
  void begin_io(uint32_t ioreq);
  void run_servers(int bit);

  uint32_t low_next_ = 0x400;
  uint32_t libs_[LIB_COUNT] = {};
  uint32_t task_ = 0;
  uint32_t td_unit_ = 0;
  uint32_t port_list_ = 0;
  uint32_t lib_list_ = 0;
  uint32_t mem_headers_[2] = {};  // chip, $C00000 expansion

  uint32_t sig_alloc_ = 0xFFFF;  // low 16 reserved for the system
  uint32_t sig_recvd_ = 0;
  uint32_t trap_alloc_ = 0;
  int id_nest_ = -1;             // exec IDNestCnt
  std::vector<uint32_t> servers_[16];  // AddIntServer chains (Interrupt*)
  uint32_t vectors_[16] = {};          // SetIntVector handlers
};

}  // namespace amiga
