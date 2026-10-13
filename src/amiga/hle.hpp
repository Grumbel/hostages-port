// High-level emulation of the parts of Kickstart the game relies on:
// exec (memory, lists, ports, interrupts, devices), trackdisk.device on the
// ADF, minimal dos/graphics bases, and the boot strap that runs the
// bootblock.
#pragma once

#include "amiga/machine.hpp"

#include <cstdint>
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

enum Lib { LIB_EXEC = 0, LIB_DOS, LIB_GFX, LIB_TRACKDISK, LIB_COUNT };

struct Hle {
  explicit Hle(Machine& m) : m(m) {}

  // Set up exec and the low-memory vectors, read the bootblock and prepare
  // the CPU to run it with an open trackdisk IORequest (like strap).
  void boot();
  void boot_done();
  void run_program();
  void call(int lib, int lvo);
  void irq(int level);
  void keyboard_irq();

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

  uint32_t sig_alloc_ = 0xFFFF;  // low 16 reserved for the system
  uint32_t sig_recvd_ = 0;
  uint32_t trap_alloc_ = 0;
  int id_nest_ = -1;             // exec IDNestCnt
  std::vector<uint32_t> servers_[16];  // AddIntServer chains (Interrupt*)
  uint32_t vectors_[16] = {};          // SetIntVector handlers
};

}  // namespace amiga
