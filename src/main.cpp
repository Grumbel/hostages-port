// Hostages — SDL2 front end.
#include "amiga/machine.hpp"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Scripted input for automated runs:  <frame> <command> [args]
//   key <SDL key name> [down|up|tap]   joy <subset of "udlrf">
//   mouse <dx> <dy>   lmb|rmb <0|1>   shot <file.ppm>   quit
//   poke <hexaddr> <hexvalue> [b|w|l]   (force a game variable)
//   pokea4 <offset> <hexvalue> [b|w|l]  (relative to the program's a4)
struct ScriptEvent {
  int frame;
  std::string cmd;
  std::vector<std::string> args;
};

std::vector<ScriptEvent> load_script(const std::string& path) {
  std::vector<ScriptEvent> ev;
  std::ifstream in(path);
  std::string line;
  int last = 0;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string f;
    ScriptEvent e;
    ss >> f >> e.cmd;
    // "+N" is relative to the previous event
    e.frame = f[0] == '+' ? last + std::atoi(f.c_str() + 1) : std::atoi(f.c_str());
    last = e.frame;
    std::string a;
    while (ss >> a) e.args.push_back(a);
    ev.push_back(e);
  }
  return ev;
}

int amiga_rawkey(SDL_Scancode sc) {
  switch (sc) {
    case SDL_SCANCODE_GRAVE: return 0x00;
    case SDL_SCANCODE_1: return 0x01; case SDL_SCANCODE_2: return 0x02;
    case SDL_SCANCODE_3: return 0x03; case SDL_SCANCODE_4: return 0x04;
    case SDL_SCANCODE_5: return 0x05; case SDL_SCANCODE_6: return 0x06;
    case SDL_SCANCODE_7: return 0x07; case SDL_SCANCODE_8: return 0x08;
    case SDL_SCANCODE_9: return 0x09; case SDL_SCANCODE_0: return 0x0A;
    case SDL_SCANCODE_MINUS: return 0x0B; case SDL_SCANCODE_EQUALS: return 0x0C;
    case SDL_SCANCODE_BACKSLASH: return 0x0D;
    case SDL_SCANCODE_Q: return 0x10; case SDL_SCANCODE_W: return 0x11;
    case SDL_SCANCODE_E: return 0x12; case SDL_SCANCODE_R: return 0x13;
    case SDL_SCANCODE_T: return 0x14; case SDL_SCANCODE_Y: return 0x15;
    case SDL_SCANCODE_U: return 0x16; case SDL_SCANCODE_I: return 0x17;
    case SDL_SCANCODE_O: return 0x18; case SDL_SCANCODE_P: return 0x19;
    case SDL_SCANCODE_LEFTBRACKET: return 0x1A; case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
    case SDL_SCANCODE_A: return 0x20; case SDL_SCANCODE_S: return 0x21;
    case SDL_SCANCODE_D: return 0x22; case SDL_SCANCODE_F: return 0x23;
    case SDL_SCANCODE_G: return 0x24; case SDL_SCANCODE_H: return 0x25;
    case SDL_SCANCODE_J: return 0x26; case SDL_SCANCODE_K: return 0x27;
    case SDL_SCANCODE_L: return 0x28; case SDL_SCANCODE_SEMICOLON: return 0x29;
    case SDL_SCANCODE_APOSTROPHE: return 0x2A;
    case SDL_SCANCODE_NONUSBACKSLASH: return 0x30;
    case SDL_SCANCODE_Z: return 0x31; case SDL_SCANCODE_X: return 0x32;
    case SDL_SCANCODE_C: return 0x33; case SDL_SCANCODE_V: return 0x34;
    case SDL_SCANCODE_B: return 0x35; case SDL_SCANCODE_N: return 0x36;
    case SDL_SCANCODE_M: return 0x37; case SDL_SCANCODE_COMMA: return 0x38;
    case SDL_SCANCODE_PERIOD: return 0x39; case SDL_SCANCODE_SLASH: return 0x3A;
    case SDL_SCANCODE_SPACE: return 0x40; case SDL_SCANCODE_BACKSPACE: return 0x41;
    case SDL_SCANCODE_TAB: return 0x42; case SDL_SCANCODE_KP_ENTER: return 0x43;
    case SDL_SCANCODE_RETURN: return 0x44; case SDL_SCANCODE_ESCAPE: return 0x45;
    case SDL_SCANCODE_DELETE: return 0x46; case SDL_SCANCODE_KP_MINUS: return 0x4A;
    case SDL_SCANCODE_UP: return 0x4C; case SDL_SCANCODE_DOWN: return 0x4D;
    case SDL_SCANCODE_RIGHT: return 0x4E; case SDL_SCANCODE_LEFT: return 0x4F;
    case SDL_SCANCODE_F1: return 0x50; case SDL_SCANCODE_F2: return 0x51;
    case SDL_SCANCODE_F3: return 0x52; case SDL_SCANCODE_F4: return 0x53;
    case SDL_SCANCODE_F5: return 0x54; case SDL_SCANCODE_F6: return 0x55;
    case SDL_SCANCODE_F7: return 0x56; case SDL_SCANCODE_F8: return 0x57;
    case SDL_SCANCODE_F9: return 0x58; case SDL_SCANCODE_F10: return 0x59;
    case SDL_SCANCODE_KP_0: return 0x0F; case SDL_SCANCODE_KP_1: return 0x1D;
    case SDL_SCANCODE_KP_2: return 0x1E; case SDL_SCANCODE_KP_3: return 0x1F;
    case SDL_SCANCODE_KP_4: return 0x2D; case SDL_SCANCODE_KP_5: return 0x2E;
    case SDL_SCANCODE_KP_6: return 0x2F; case SDL_SCANCODE_KP_7: return 0x3D;
    case SDL_SCANCODE_KP_8: return 0x3E; case SDL_SCANCODE_KP_9: return 0x3F;
    case SDL_SCANCODE_KP_PERIOD: return 0x3C; case SDL_SCANCODE_KP_DIVIDE: return 0x5C;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x5D; case SDL_SCANCODE_KP_PLUS: return 0x5E;
    case SDL_SCANCODE_HOME: return 0x5F;  // HELP
    case SDL_SCANCODE_LSHIFT: return 0x60; case SDL_SCANCODE_RSHIFT: return 0x61;
    case SDL_SCANCODE_CAPSLOCK: return 0x62; case SDL_SCANCODE_LCTRL: return 0x63;
    case SDL_SCANCODE_LALT: return 0x64; case SDL_SCANCODE_RALT: return 0x65;
    case SDL_SCANCODE_LGUI: return 0x66; case SDL_SCANCODE_RGUI: return 0x67;
    default: return -1;
  }
}


// Tiny 5×7 bitmap font (ASCII 32–126). Each glyph is 5 columns packed into
// 7 rows of bits (LSB = leftmost pixel). Used for the debug menu overlay.
constexpr int FONT_W = 5, FONT_H = 7, FONT_ADV = 6;
const uint8_t kFont[95][7] = {
  // space .. /  (32-47)
  {0,0,0,0,0,0,0},{0x04,0x04,0x04,0x04,0x00,0x04,0},{0x0A,0x0A,0x00,0,0,0,0},
  {0x0A,0x1F,0x0A,0x1F,0x0A,0,0},{0x04,0x1E,0x05,0x0E,0x14,0x0F,0x04},
  {0x19,0x19,0x02,0x04,0x08,0x13,0x13},{0x02,0x05,0x05,0x02,0x15,0x09,0x16},
  {0x04,0x04,0,0,0,0,0},{0x04,0x02,0x01,0x01,0x01,0x02,0x04},
  {0x04,0x08,0x10,0x10,0x10,0x08,0x04},{0x00,0x0A,0x04,0x1F,0x04,0x0A,0},
  {0,0x04,0x04,0x1F,0x04,0x04,0},{0,0,0,0,0x04,0x04,0x02},
  {0,0,0,0x1F,0,0,0},{0,0,0,0,0,0x04,0},{0x10,0x08,0x04,0x02,0x01,0,0},
  // 0-9
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x06,0x04,0x04,0x04,0x04,0x0E},
  {0x0E,0x11,0x10,0x0C,0x02,0x01,0x1F},{0x0E,0x11,0x10,0x0C,0x10,0x11,0x0E},
  {0x08,0x0C,0x0A,0x09,0x1F,0x08,0x08},{0x1F,0x01,0x0F,0x10,0x10,0x11,0x0E},
  {0x0C,0x02,0x01,0x0F,0x11,0x11,0x0E},{0x1F,0x10,0x08,0x04,0x02,0x02,0x02},
  {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x1E,0x10,0x08,0x06},
  // : ; < = > ? @
  {0,0x04,0,0,0x04,0,0},{0,0x04,0,0,0x04,0x04,0x02},
  {0x08,0x04,0x02,0x01,0x02,0x04,0x08},{0,0,0x1F,0,0x1F,0,0},
  {0x02,0x04,0x08,0x10,0x08,0x04,0x02},{0x0E,0x11,0x10,0x08,0x04,0,0x04},
  {0x0E,0x11,0x15,0x15,0x0D,0x01,0x1E},
  // A-Z
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x0F,0x11,0x11,0x0F,0x11,0x11,0x0F},
  {0x0E,0x11,0x01,0x01,0x01,0x11,0x0E},{0x0F,0x11,0x11,0x11,0x11,0x11,0x0F},
  {0x1F,0x01,0x01,0x0F,0x01,0x01,0x1F},{0x1F,0x01,0x01,0x0F,0x01,0x01,0x01},
  {0x0E,0x11,0x01,0x1D,0x11,0x11,0x0E},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
  {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},{0x10,0x10,0x10,0x10,0x10,0x11,0x0E},
  {0x11,0x09,0x05,0x03,0x05,0x09,0x11},{0x01,0x01,0x01,0x01,0x01,0x01,0x1F},
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x13,0x15,0x19,0x11,0x11,0x11},
  {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},{0x0F,0x11,0x11,0x0F,0x01,0x01,0x01},
  {0x0E,0x11,0x11,0x11,0x15,0x09,0x16},{0x0F,0x11,0x11,0x0F,0x05,0x09,0x11},
  {0x0E,0x11,0x01,0x0E,0x10,0x11,0x0E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
  {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
  {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
  {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x10,0x08,0x04,0x02,0x01,0x1F},
  // [ \ ] ^ _ `
  {0x07,0x01,0x01,0x01,0x01,0x01,0x07},{0x01,0x02,0x04,0x08,0x10,0,0},
  {0x1C,0x10,0x10,0x10,0x10,0x10,0x1C},{0x04,0x0A,0x11,0,0,0,0},
  {0,0,0,0,0,0,0x1F},{0x02,0x04,0,0,0,0,0},
  // a-z (reuse uppercase shapes for the tiny font)
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x0F,0x11,0x11,0x0F,0x11,0x11,0x0F},
  {0x0E,0x11,0x01,0x01,0x01,0x11,0x0E},{0x0F,0x11,0x11,0x11,0x11,0x11,0x0F},
  {0x1F,0x01,0x01,0x0F,0x01,0x01,0x1F},{0x1F,0x01,0x01,0x0F,0x01,0x01,0x01},
  {0x0E,0x11,0x01,0x1D,0x11,0x11,0x0E},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
  {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},{0x10,0x10,0x10,0x10,0x10,0x11,0x0E},
  {0x11,0x09,0x05,0x03,0x05,0x09,0x11},{0x01,0x01,0x01,0x01,0x01,0x01,0x1F},
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x13,0x15,0x19,0x11,0x11,0x11},
  {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},{0x0F,0x11,0x11,0x0F,0x01,0x01,0x01},
  {0x0E,0x11,0x11,0x11,0x15,0x09,0x16},{0x0F,0x11,0x11,0x0F,0x05,0x09,0x11},
  {0x0E,0x11,0x01,0x0E,0x10,0x11,0x0E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
  {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
  {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
  {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x10,0x08,0x04,0x02,0x01,0x1F},
  // { | } ~
  {0x0C,0x02,0x02,0x01,0x02,0x02,0x0C},{0x04,0x04,0x04,0x04,0x04,0x04,0x04},
  {0x06,0x08,0x08,0x10,0x08,0x08,0x06},{0x08,0x15,0x02,0,0,0,0},
};

void draw_char(uint32_t* fb, int stride, int x, int y, char ch, uint32_t color) {
  if (ch < 32 || ch > 126) ch = '?';
  const uint8_t* g = kFont[ch - 32];
  for (int row = 0; row < FONT_H; ++row) {
    uint8_t bits = g[row];
    for (int col = 0; col < FONT_W; ++col)
      if (bits & (1 << col)) {
        int px = x + col, py = y + row;
        if (px >= 0 && px < amiga::FB_W && py >= 0 && py < amiga::VIEW_H)
          fb[py * stride + px] = color;
      }
  }
}

void draw_text(uint32_t* fb, int stride, int x, int y, const char* s, uint32_t color) {
  for (; *s; ++s) {
    draw_char(fb, stride, x, y, *s, color);
    x += FONT_ADV;
  }
}

void fill_rect(uint32_t* fb, int stride, int x, int y, int w, int h, uint32_t color) {
  for (int row = 0; row < h; ++row) {
    int py = y + row;
    if (py < 0 || py >= amiga::VIEW_H) continue;
    for (int col = 0; col < w; ++col) {
      int px = x + col;
      if (px >= 0 && px < amiga::FB_W) fb[py * stride + px] = color;
    }
  }
}

class SdlHost : public amiga::Host {
public:
  SDL_Window* win = nullptr;
  SDL_Renderer* ren = nullptr;
  SDL_Texture* tex = nullptr;
  SDL_AudioDeviceID audio_dev = 0;
  amiga::Machine* machine = nullptr;
  SDL_GameController* pad = nullptr;
  bool mouse_grab = false;
  bool fast = false;
  bool quit = false;
  uint64_t next_frame = 0;
  int screenshot_frame = -1;
  std::string screenshot_path;
  int frame_no = 0;
  int quit_after = -1;
  std::vector<ScriptEvent> script;
  size_t script_pos = 0;
  std::vector<std::pair<int, int>> pending_up;  // frame, rawkey
  bool sjoy[5] = {};  // scripted joystick
  bool kjoy[5] = {};  // keyboard joystick
  bool pad_held_key[128] = {};

  // Debug menu (opened with controller Back or F9).
  bool menu_open = false;
  int menu_sel = 0;
  int save_slot = 1;  // 1..5
  std::string save_dir;
  std::string status_msg;
  int status_until = 0;  // frame_no when status_msg clears
  bool pad_back_held = false;
  bool pad_menu_nav[6] = {};  // up down left right a confirm edge track
  // After closing the menu, ignore game input until the key/button that
  // closed it (and other face controls) are released, so the game does
  // not see the same press as fire/Esc/Return.
  bool menu_wait_release = false;

  // Character / team select menu (controller NORTH / Y). Replaces F1–F6 on
  // a gamepad: stage I lists DELTA/ECHO/MIKE, stage II all six. When stage I
  // targets are filled, also offers Continue (Return) and Restart (F10).
  bool char_menu_open = false;
  int char_menu_sel = 0;
  bool pad_y_held = false;
  bool pad_char_nav[5] = {};

  static constexpr const char* kTeamNames[] = {
      "DELTA", "ECHO", "MIKE", "HOTEL", "TANGO", "BRAVO"};
  static constexpr int kTeamCountStage1 = 3;
  static constexpr int kTeamCountStage2 = 6;

  enum MenuItem {
    MI_RESUME = 0,
    MI_SAVE,
    MI_LOAD,
    MI_SLOT,
    MI_COMPLETE_S1,
    MI_SOFT_RESET,
    MI_SCREENSHOT,
    MI_FAST,
    MI_COUNT
  };

  static const char* menu_label(int i, int slot, bool fast_on) {
    switch (i) {
      case MI_RESUME: return "Resume";
      case MI_SAVE: return "Save state";
      case MI_LOAD: return "Load state";
      case MI_SLOT: return nullptr;  // formatted
      case MI_COMPLETE_S1: return "Complete stage I";
      case MI_SOFT_RESET: return "Soft reset";
      case MI_SCREENSHOT: return "Screenshot";
      case MI_FAST: return fast_on ? "Fast forward: ON" : "Fast forward: OFF";
      default: return "?";
    }
  }

  std::string slot_path(int slot) const {
    return save_dir + "/states/slot" + std::to_string(slot) + ".sav";
  }

  void set_status(const std::string& s) {
    status_msg = s;
    status_until = frame_no + 90;
  }

  void open_menu() {
    if (char_menu_open) close_char_menu();
    menu_open = true;
    menu_sel = 0;
    if (machine) machine->debug_pause = true;
  }

  void close_menu() {
    menu_open = false;
    menu_wait_release = true;
    if (machine && !char_menu_open) machine->debug_pause = false;
  }

  // --- Character select menu -----------------------------------------------
  // Stage I map flag: -550(a4) is set by the plan/map init ($43EC) and
  // cleared when the player takes control of a marksman in the street
  // ($62CE). F1–F3 only do anything while this is non-zero.
  bool stage1_on_map() const {
    if (!machine || machine->program_a4() == 0) return false;
    if (machine->program_name() != "apro1.") return false;
    return machine->r16(machine->program_a4() + uint32_t(-550)) != 0;
  }

  bool stage1_targets_ready() const {
    if (!machine || machine->program_a4() == 0) return false;
    if (machine->program_name() != "apro1.") return false;
    // Same offsets as Machine::complete_stage1().
    const int base = machine->disk.is_dos() ? -7600 : -206;
    for (int i = 0; i < 3; ++i) {
      uint32_t a = machine->program_a4() + uint32_t(base + i * 14);
      if (machine->r16(a) == 0) return false;
    }
    return true;
  }

  int char_menu_count() const {
    if (!machine) return 0;
    const std::string& pn = machine->program_name();
    if (pn == "apro1.") {
      // Team pick only on the map (F1–F3 are inert in the street). When
      // targets are set, still offer Continue/Restart from the street too.
      if (!stage1_on_map() && !stage1_targets_ready()) return 0;
      int n = stage1_on_map() ? kTeamCountStage1 : 0;
      if (stage1_targets_ready()) n += 2;  // Continue + Restart
      return n;
    }
    if (pn == "it.") return kTeamCountStage2;
    return 0;
  }

  // How many leading rows are team members (vs Continue/Restart at the end).
  int char_menu_member_rows() const {
    if (!machine) return 0;
    if (machine->program_name() == "apro1.")
      return stage1_on_map() ? kTeamCountStage1 : 0;
    if (machine->program_name() == "it.") return kTeamCountStage2;
    return 0;
  }

  std::string char_menu_label(int i) const {
    int n = char_menu_count();
    if (i < 0 || i >= n) return "?";
    const int members = char_menu_member_rows();
    if (i < members) {
      char buf[24];
      std::snprintf(buf, sizeof(buf), "F%d %s", i + 1, kTeamNames[i]);
      return buf;
    }
    // Continue / Restart trail after the member rows.
    if (i == members) return "Continue (Return)";
    return "Restart (F10)";
  }

  // True for a team-member row (not Continue/Restart). Snipers are 0..2;
  // assault (enter the house) are 3..5 in stage II.
  bool char_menu_is_member(int i) const {
    return i >= 0 && i < char_menu_member_rows();
  }

  bool char_menu_is_sniper(int i) const { return char_menu_is_member(i) && i < 3; }
  bool char_menu_is_assault(int i) const { return char_menu_is_member(i) && i >= 3; }

  // Stage I marksman record: -20902(a4) + 28*i; +18 alive, +24 hit count.
  // Stage II table: -32284(a4) + 8*i; word 0 set when activated.
  uint16_t team_member_status(int i) const {
    if (!machine || machine->program_a4() == 0 || !char_menu_is_member(i)) return 1;
    const uint32_t a4 = machine->program_a4();
    if (machine->program_name() == "apro1.")
      return machine->r16(a4 + uint32_t(-20902 + i * 28 + 18));
    if (machine->program_name() == "it.")
      return machine->r16(a4 + uint32_t(-32284 + i * 8));
    return 1;
  }

  uint16_t team_member_hits(int i) const {
    if (!machine || machine->program_a4() == 0 || !char_menu_is_member(i)) return 0;
    if (machine->program_name() != "apro1.") return 0;
    return machine->r16(machine->program_a4() + uint32_t(-20902 + i * 28 + 24));
  }

  // Stage I: placed+alive / not yet placed / dead (hits accumulated).
  // Stage II: activated (status≠0) / not yet / dead (status 0 after any activation).
  enum TeamState { TS_ALIVE, TS_UNSPAWNED, TS_DEAD };

  TeamState team_member_state(int i) const {
    if (!char_menu_is_member(i)) return TS_ALIVE;
    const uint16_t st = team_member_status(i);
    if (st != 0) return TS_ALIVE;
    if (machine && machine->program_name() == "apro1.") {
      // Hit counter only climbs while the man is in play; stays 0 if never placed.
      return team_member_hits(i) > 0 ? TS_DEAD : TS_UNSPAWNED;
    }
    // Stage II: any non-zero slot means the table is live; zero = dead.
    for (int j = 0; j < kTeamCountStage2; ++j)
      if (j != i && team_member_status(j) != 0) return TS_DEAD;
    return TS_UNSPAWNED;
  }

  // Colour: dead grey; unspawned dim; snipers cyan; assault amber.
  uint32_t char_menu_color(int i) const {
    const bool sel = (i == char_menu_sel);
    if (char_menu_is_member(i)) {
      switch (team_member_state(i)) {
        case TS_DEAD:
          return sel ? 0xFF909090 : 0xFF505050;
        case TS_UNSPAWNED:
          // Dim version of the role colour.
          if (char_menu_is_assault(i))
            return sel ? 0xFFB08040 : 0xFF705020;
          return sel ? 0xFF608090 : 0xFF305060;
        case TS_ALIVE:
          if (char_menu_is_assault(i))
            return sel ? 0xFFFFD080 : 0xFFE0A040;
          return sel ? 0xFF80FFFF : 0xFF40C0E0;
      }
    }
    // Continue / Restart
    return sel ? 0xFFFFFF40 : 0xFFD0D0D0;
  }

  void open_char_menu() {
    if (char_menu_count() == 0) return;
    if (menu_open) close_menu();
    char_menu_open = true;
    char_menu_sel = 0;
    if (machine) machine->debug_pause = true;
  }

  void close_char_menu() {
    char_menu_open = false;
    menu_wait_release = true;
    if (machine && !menu_open) machine->debug_pause = false;
  }

  void activate_char_menu() {
    if (!machine) return;
    const int n = char_menu_count();
    if (char_menu_sel < 0 || char_menu_sel >= n) return;
    // Dead members stay listed but are not selectable; unspawned still are
    // (F1–F6 activates them).
    if (char_menu_is_member(char_menu_sel) &&
        team_member_state(char_menu_sel) == TS_DEAD)
      return;
    int rk = -1;
    const int members = char_menu_member_rows();
    if (char_menu_sel < members)
      rk = 0x50 + char_menu_sel;  // F1..F6
    else if (char_menu_sel == members)
      rk = 0x44;  // Return / Continue
    else
      rk = 0x59;  // F10 / Restart
    close_char_menu();
    if (rk >= 0) {
      machine->key_event(uint8_t(rk), true);
      pending_up.push_back({frame_no + 4, rk});
    }
  }

  void draw_char_menu_overlay(uint32_t* fb) {
    for (int i = 0; i < amiga::FB_W * amiga::VIEW_H; ++i) {
      uint32_t c = fb[i];
      uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
      fb[i] = 0xFF000000u | ((r / 3) << 16) | ((g / 3) << 8) | (b / 3);
    }
    const int n = char_menu_count();
    constexpr int box_x = 70, box_y = 40, box_w = 180;
    const int box_h = 28 + n * 12 + 16;
    fill_rect(fb, amiga::FB_W, box_x, box_y, box_w, box_h, 0xFF101820);
    fill_rect(fb, amiga::FB_W, box_x, box_y, box_w, 1, 0xFF40FF80);
    fill_rect(fb, amiga::FB_W, box_x, box_y + box_h - 1, box_w, 1, 0xFF40FF80);
    fill_rect(fb, amiga::FB_W, box_x, box_y, 1, box_h, 0xFF40FF80);
    fill_rect(fb, amiga::FB_W, box_x + box_w - 1, box_y, 1, box_h, 0xFF40FF80);

    draw_text(fb, amiga::FB_W, box_x + 8, box_y + 6, "SELECT TEAM", 0xFF40FF80);
    for (int i = 0; i < n; ++i) {
      int y = box_y + 22 + i * 12;
      if (i == char_menu_sel)
        fill_rect(fb, amiga::FB_W, box_x + 4, y - 1, box_w - 8, 10, 0xFF203040);
      draw_text(fb, amiga::FB_W, box_x + 12, y, char_menu_label(i).c_str(), char_menu_color(i));
    }
    draw_text(fb, amiga::FB_W, box_x + 8, box_y + box_h - 12,
              "Up/Down  A  Y/B", 0xFF607080);
  }

  void activate_menu() {
    if (!machine) return;
    switch (menu_sel) {
      case MI_RESUME:
        close_menu();
        break;
      case MI_SAVE: {
        std::filesystem::create_directories(save_dir + "/states");
        std::string p = slot_path(save_slot);
        if (machine->save_state(p)) set_status("Saved slot " + std::to_string(save_slot));
        else set_status("Save failed");
        break;
      }
      case MI_LOAD: {
        std::string p = slot_path(save_slot);
        if (machine->load_state(p)) {
          set_status("Loaded slot " + std::to_string(save_slot));
          // frame counter on the host is independent; keep going
        } else set_status("Load failed");
        break;
      }
      case MI_SLOT:
        save_slot = save_slot % 5 + 1;
        break;
      case MI_COMPLETE_S1:
        machine->complete_stage1();
        set_status("Stage I marked complete");
        break;
      case MI_SOFT_RESET:
        if (machine->soft_reset()) set_status("Soft reset");
        else set_status("Reset failed");
        close_menu();
        break;
      case MI_SCREENSHOT: {
        pending_shot = "debug_shot.ppm";
        set_status("Screenshot queued");
        break;
      }
      case MI_FAST:
        fast = !fast;
        break;
      default: break;
    }
  }

  void draw_menu_overlay(uint32_t* fb) {
    // Dim the background.
    for (int i = 0; i < amiga::FB_W * amiga::VIEW_H; ++i) {
      uint32_t c = fb[i];
      uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
      fb[i] = 0xFF000000u | ((r / 3) << 16) | ((g / 3) << 8) | (b / 3);
    }
    constexpr int box_x = 40, box_y = 28, box_w = 240, box_h = 150;
    fill_rect(fb, amiga::FB_W, box_x, box_y, box_w, box_h, 0xFF101820);
    fill_rect(fb, amiga::FB_W, box_x, box_y, box_w, 1, 0xFF40A0FF);
    fill_rect(fb, amiga::FB_W, box_x, box_y + box_h - 1, box_w, 1, 0xFF40A0FF);
    fill_rect(fb, amiga::FB_W, box_x, box_y, 1, box_h, 0xFF40A0FF);
    fill_rect(fb, amiga::FB_W, box_x + box_w - 1, box_y, 1, box_h, 0xFF40A0FF);

    draw_text(fb, amiga::FB_W, box_x + 8, box_y + 6, "DEBUG MENU", 0xFF40A0FF);
    char info[64];
    std::snprintf(info, sizeof(info), "frame %d  %s", frame_no,
                  machine && !machine->program_name().empty()
                      ? machine->program_name().c_str()
                      : "-");
    draw_text(fb, amiga::FB_W, box_x + 8, box_y + 16, info, 0xFF808890);

    for (int i = 0; i < MI_COUNT; ++i) {
      int y = box_y + 30 + i * 12;
      uint32_t col = (i == menu_sel) ? 0xFFFFFF40 : 0xFFD0D0D0;
      if (i == menu_sel)
        fill_rect(fb, amiga::FB_W, box_x + 4, y - 1, box_w - 8, 10, 0xFF203040);
      char line[64];
      if (i == MI_SLOT)
        std::snprintf(line, sizeof(line), "Slot: %d", save_slot);
      else
        std::snprintf(line, sizeof(line), "%s", menu_label(i, save_slot, fast));
      draw_text(fb, amiga::FB_W, box_x + 12, y, line, col);
    }

    draw_text(fb, amiga::FB_W, box_x + 8, box_y + box_h - 14,
              "Up/Down  A/Return  Back", 0xFF607080);
    if (!status_msg.empty() && frame_no < status_until)
      draw_text(fb, amiga::FB_W, box_x + 8, box_y + box_h - 24, status_msg.c_str(),
                0xFF80FF80);
  }

  bool init(int scale) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
      fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
      return false;
    }
    win = SDL_CreateWindow("Hostages", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           amiga::FB_W * scale, amiga::VIEW_H * scale * 15 / 16,
                           SDL_WINDOW_RESIZABLE);
    if (!win) return false;
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    // PAL lores pixels are slightly wider than tall (a 320×256 frame is 4:3).
    SDL_RenderSetLogicalSize(ren, amiga::FB_W, amiga::VIEW_H * 15 / 16);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                            amiga::FB_W, amiga::VIEW_H);
    SDL_AudioSpec want{}, have{};
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    audio_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audio_dev) SDL_PauseAudioDevice(audio_dev, 0);
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
      if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }
    next_frame = SDL_GetPerformanceCounter();
    return true;
  }

  void set_grab(bool g) {
    mouse_grab = g;
    SDL_SetRelativeMouseMode(g ? SDL_TRUE : SDL_FALSE);
  }

  void handle_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_QUIT: quit = true; break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
          bool down = e.type == SDL_KEYDOWN;
          SDL_Scancode sc = e.key.keysym.scancode;
          if (sc == SDL_SCANCODE_F12 && down) { set_grab(!mouse_grab); break; }
          if (sc == SDL_SCANCODE_F11 && down) {
            Uint32 fl = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
            SDL_SetWindowFullscreen(win, fl ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            break;
          }
          if (sc == SDL_SCANCODE_PAUSE && down && !menu_open && !char_menu_open) { fast = !fast; break; }
          if (sc == SDL_SCANCODE_F4 && down && (e.key.keysym.mod & KMOD_ALT)) { quit = true; break; }
          // F9 toggles the debug menu (keyboard equivalent of controller Back).
          if (sc == SDL_SCANCODE_F9 && down && !menu_wait_release) {
            if (menu_open) close_menu();
            else if (char_menu_open) close_char_menu();
            else open_menu();
            break;
          }
          if (menu_wait_release) {
            // Swallow events until the closing key/button is up.
            if (!down && !menu_controls_held()) menu_wait_release = false;
            break;
          }
          if (menu_open) {
            if (!down || e.key.repeat) break;
            if (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_W) {
              menu_sel = (menu_sel + MI_COUNT - 1) % MI_COUNT;
            } else if (sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_S) {
              menu_sel = (menu_sel + 1) % MI_COUNT;
            } else if (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_A) {
              if (menu_sel == MI_SLOT) save_slot = save_slot <= 1 ? 5 : save_slot - 1;
            } else if (sc == SDL_SCANCODE_RIGHT || sc == SDL_SCANCODE_D) {
              if (menu_sel == MI_SLOT) save_slot = save_slot % 5 + 1;
            } else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_SPACE ||
                       sc == SDL_SCANCODE_Z || sc == SDL_SCANCODE_X) {
              activate_menu();
            } else if (sc == SDL_SCANCODE_ESCAPE) {
              close_menu();
            }
            break;
          }
          if (char_menu_open) {
            if (!down || e.key.repeat) break;
            const int n = char_menu_count();
            if (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_W) {
              if (n > 0) char_menu_sel = (char_menu_sel + n - 1) % n;
            } else if (sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_S) {
              if (n > 0) char_menu_sel = (char_menu_sel + 1) % n;
            } else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_SPACE ||
                       sc == SDL_SCANCODE_Z || sc == SDL_SCANCODE_X) {
              activate_char_menu();
            } else if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_Y) {
              close_char_menu();
            }
            break;
          }
          if (e.key.repeat) break;
          int rk = amiga_rawkey(sc);
          if (rk >= 0) machine->key_event(uint8_t(rk), down);
          // Cursor keys and right Ctrl also work as joystick in port 2.
          switch (sc) {
            case SDL_SCANCODE_UP: kjoy[0] = down; break;
            case SDL_SCANCODE_DOWN: kjoy[1] = down; break;
            case SDL_SCANCODE_LEFT: kjoy[2] = down; break;
            case SDL_SCANCODE_RIGHT: kjoy[3] = down; break;
            case SDL_SCANCODE_RCTRL: kjoy[4] = down; break;
            default: break;
          }
          break;
        }
        case SDL_MOUSEMOTION:
          if (mouse_grab) machine->mouse_move(e.motion.xrel, e.motion.yrel);
          break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
          bool down = e.type == SDL_MOUSEBUTTONDOWN;
          // Mouse grab is opt-in (--grab or F12); do not steal the pointer on click.
          if (e.button.button == SDL_BUTTON_LEFT) machine->mouse_button(0, down);
          if (e.button.button == SDL_BUTTON_RIGHT) machine->mouse_button(1, down);
          break;
        }
        case SDL_CONTROLLERDEVICEADDED:
          if (!pad) pad = SDL_GameControllerOpen(e.cdevice.which);
          break;
      }
    }
    run_script();
    // Joystick in port 2 from script, keyboard and game controller.
    bool up = false, down = false, left = false, right = false, fire = false;
    const bool any_menu = menu_open || char_menu_open;
    if (!any_menu && !menu_wait_release) {
      up = sjoy[0] || kjoy[0];
      down = sjoy[1] || kjoy[1];
      left = sjoy[2] || kjoy[2];
      right = sjoy[3] || kjoy[3];
      fire = sjoy[4] || kjoy[4];
    }
    if (pad) poll_gamepad(up, down, left, right, fire);
    else if (menu_wait_release && !menu_controls_held()) {
      menu_wait_release = false;
    }
    if (!any_menu && !menu_wait_release)
      machine->joy_state(up, down, left, right, fire);
    else
      machine->joy_state(false, false, false, false, false);
  }

  // True while any control that can open/close/activate the menu is held.
  bool menu_controls_held() const {
    if (pad) {
      auto btn = [&](SDL_GameControllerButton b) {
        return SDL_GameControllerGetButton(pad, b) != 0;
      };
      if (btn(SDL_CONTROLLER_BUTTON_BACK) || btn(SDL_CONTROLLER_BUTTON_A) ||
          btn(SDL_CONTROLLER_BUTTON_B) || btn(SDL_CONTROLLER_BUTTON_X) ||
          btn(SDL_CONTROLLER_BUTTON_Y) || btn(SDL_CONTROLLER_BUTTON_START))
        return true;
    }
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    if (ks[SDL_SCANCODE_F9] || ks[SDL_SCANCODE_ESCAPE] || ks[SDL_SCANCODE_RETURN] ||
        ks[SDL_SCANCODE_SPACE] || ks[SDL_SCANCODE_Z] || ks[SDL_SCANCODE_X] ||
        ks[SDL_SCANCODE_Y])
      return true;
    return false;
  }

  void poll_gamepad(bool& up, bool& down, bool& left, bool& right, bool& fire) {
    auto btn = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(pad, b) != 0; };
    constexpr int dead = 12000;
    int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    int ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);

    // After closing, wait until the closing control is released so the game
    // does not see the same press (e.g. A as fire, Back as a second edge).
    if (menu_wait_release) {
      pad_back_held = btn(SDL_CONTROLLER_BUTTON_BACK);
      pad_y_held = btn(SDL_CONTROLLER_BUTTON_Y);
      if (!menu_controls_held()) menu_wait_release = false;
      up = down = left = right = fire = false;
      return;
    }

    // Back button opens/closes the debug menu (edge-triggered).
    bool back = btn(SDL_CONTROLLER_BUTTON_BACK);
    if (back && !pad_back_held) {
      if (menu_open) close_menu();
      else if (char_menu_open) close_char_menu();
      else open_menu();
    }
    pad_back_held = back;

    // NORTH (Y) opens/closes the character select menu.
    bool ybtn = btn(SDL_CONTROLLER_BUTTON_Y);
    if (ybtn && !pad_y_held && !menu_open) {
      if (char_menu_open) close_char_menu();
      else open_char_menu();
    }
    pad_y_held = ybtn;

    // close_*() sets menu_wait_release; do not feed the still-held button
    // into the game on this same frame.
    if (menu_wait_release) {
      up = down = left = right = fire = false;
      return;
    }

    if (menu_open) {
      // Edge-triggered navigation while the debug menu is open.
      bool nav[5] = {
          ly < -dead || btn(SDL_CONTROLLER_BUTTON_DPAD_UP),
          ly > dead || btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN),
          lx < -dead || btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT),
          lx > dead || btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT),
          btn(SDL_CONTROLLER_BUTTON_A) || btn(SDL_CONTROLLER_BUTTON_X),
      };
      if (nav[0] && !pad_menu_nav[0]) menu_sel = (menu_sel + MI_COUNT - 1) % MI_COUNT;
      if (nav[1] && !pad_menu_nav[1]) menu_sel = (menu_sel + 1) % MI_COUNT;
      if (nav[2] && !pad_menu_nav[2] && menu_sel == MI_SLOT)
        save_slot = save_slot <= 1 ? 5 : save_slot - 1;
      if (nav[3] && !pad_menu_nav[3] && menu_sel == MI_SLOT)
        save_slot = save_slot % 5 + 1;
      if (nav[4] && !pad_menu_nav[4]) activate_menu();
      for (int i = 0; i < 5; ++i) pad_menu_nav[i] = nav[i];
      up = down = left = right = fire = false;
      return;
    }

    if (char_menu_open) {
      const int n = char_menu_count();
      bool nav[5] = {
          ly < -dead || btn(SDL_CONTROLLER_BUTTON_DPAD_UP),
          ly > dead || btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN),
          false,
          false,
          btn(SDL_CONTROLLER_BUTTON_A) || btn(SDL_CONTROLLER_BUTTON_X),
      };
      // B also closes.
      if (btn(SDL_CONTROLLER_BUTTON_B) && !pad_char_nav[2]) {
        close_char_menu();
        pad_char_nav[2] = true;
        up = down = left = right = fire = false;
        return;
      }
      if (!btn(SDL_CONTROLLER_BUTTON_B)) pad_char_nav[2] = false;
      if (n > 0) {
        if (nav[0] && !pad_char_nav[0]) char_menu_sel = (char_menu_sel + n - 1) % n;
        if (nav[1] && !pad_char_nav[1]) char_menu_sel = (char_menu_sel + 1) % n;
        if (nav[4] && !pad_char_nav[4]) activate_char_menu();
      }
      for (int i = 0; i < 5; ++i) if (i != 2) pad_char_nav[i] = nav[i];
      up = down = left = right = fire = false;
      return;
    }

    up |= ly < -dead || btn(SDL_CONTROLLER_BUTTON_DPAD_UP);
    down |= ly > dead || btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    left |= lx < -dead || btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    right |= lx > dead || btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    fire |= btn(SDL_CONTROLLER_BUTTON_A);
    // Other buttons send keys (held while the button is held).
    // Back → debug menu, Y → character menu (above).
    // Start and X both mean Return (continue); OR them so releasing one while
    // the other is held does not drop the key. Restart (F10) is only offered
    // from the team menu when stage I targets are set.
    static const struct { SDL_GameControllerButton b; int key; } map[] = {
        {SDL_CONTROLLER_BUTTON_B, 0x45},              // Esc (EAST)
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0x50},   // F1
        {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0x51},  // F2
    };
    for (auto& m : map) {
      bool d = btn(m.b);
      if (d != pad_held_key[m.key]) {
        pad_held_key[m.key] = d;
        machine->key_event(uint8_t(m.key), d);
      }
    }
    {
      bool ret = btn(SDL_CONTROLLER_BUTTON_X) || btn(SDL_CONTROLLER_BUTTON_START);
      if (ret != pad_held_key[0x44]) {
        pad_held_key[0x44] = ret;
        machine->key_event(0x44, ret);
      }
    }
  }

  void frame(const uint32_t* fb) override {
    ++frame_no;
    // Copy into a mutable buffer so overlays can composite on top.
    static std::vector<uint32_t> overlay;
    overlay.assign(fb, fb + amiga::FB_W * amiga::VIEW_H);
    if (menu_open) draw_menu_overlay(overlay.data());
    else if (char_menu_open) draw_char_menu_overlay(overlay.data());
    else if (!status_msg.empty() && frame_no < status_until)
      draw_text(overlay.data(), amiga::FB_W, 4, 4, status_msg.c_str(), 0xFF80FF80);
    SDL_UpdateTexture(tex, nullptr, overlay.data(), amiga::FB_W * 4);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);
    if (frame_no == screenshot_frame) save_screenshot(fb);
    if (!pending_shot.empty()) {
      std::swap(pending_shot, screenshot_path);
      save_screenshot(fb);
      std::swap(pending_shot, screenshot_path);
      pending_shot.clear();
    }
    if (quit_after >= 0 && frame_no >= quit_after) quit = true;
    handle_events();
    if (quit) throw amiga::QuitRequested{};
    // Pace to the PAL frame rate unless fast-forwarding.
    uint64_t freq = SDL_GetPerformanceFrequency();
    next_frame += uint64_t(double(freq) / amiga::FRAME_HZ);
    uint64_t now = SDL_GetPerformanceCounter();
    if (fast || int64_t(next_frame - now) < -int64_t(freq / 10)) next_frame = now;
    // Signed arithmetic: the counter may pass next_frame between reads.
    while (!fast) {
      int64_t left = int64_t(next_frame - SDL_GetPerformanceCounter());
      if (left <= 0) break;
      int64_t left_us = left * 1000000 / int64_t(freq);
      if (left_us > 2000) SDL_Delay(uint32_t(left_us / 1000 - 1));
    }
  }

  void run_script() {
    for (size_t i = 0; i < pending_up.size();) {
      if (pending_up[i].first <= frame_no) {
        machine->key_event(uint8_t(pending_up[i].second), false);
        pending_up.erase(pending_up.begin() + long(i));
      } else {
        ++i;
      }
    }
    while (script_pos < script.size() && script[script_pos].frame <= frame_no) {
      const ScriptEvent& e = script[script_pos++];
      if (e.cmd == "key" && !e.args.empty()) {
        static const std::pair<const char*, const char*> alias[] = {
            {"lshift", "Left Shift"}, {"rshift", "Right Shift"}, {"ctrl", "Left Ctrl"},
            {"lalt", "Left Alt"}, {"ralt", "Right Alt"}, {"equals", "="}, {"minus", "-"},
            {"comma", ","}, {"period", "."}, {"enter", "Keypad Enter"}};
        std::string name = e.args[0];
        for (auto& [a, b] : alias) if (name == a) name = b;
        int rk = amiga_rawkey(SDL_GetScancodeFromName(name.c_str()));
        if (rk < 0) rk = int(std::strtol(name.c_str(), nullptr, 16));
        // Keys are held long enough for the game's once-per-frame polling.
        std::string mode = e.args.size() > 1 ? e.args[1] : "tap";
        if (mode == "down") machine->key_event(uint8_t(rk), true);
        else if (mode == "up") machine->key_event(uint8_t(rk), false);
        else { machine->key_event(uint8_t(rk), true); pending_up.push_back({frame_no + 4, rk}); }
      } else if (e.cmd == "joy") {
        std::string st = e.args.empty() ? "" : e.args[0];
        const char* names = "udlrf";
        for (int k = 0; k < 5; ++k) sjoy[k] = st.find(names[k]) != std::string::npos;
      } else if (e.cmd == "mouse" && e.args.size() >= 2) {
        machine->mouse_move(std::atoi(e.args[0].c_str()), std::atoi(e.args[1].c_str()));
      } else if ((e.cmd == "lmb" || e.cmd == "rmb") && !e.args.empty()) {
        machine->mouse_button(e.cmd == "lmb" ? 0 : 1, e.args[0] != "0");
      } else if (e.cmd == "pokea4" && e.args.size() >= 2) {
        // pokea4 <signed decimal offset> <hexvalue> [b|w|l]: relative to the
        // running program's small-data base (offsets as in the disassembly)
        uint32_t a = machine->program_a4() + uint32_t(std::atoi(e.args[0].c_str()));
        uint32_t v = uint32_t(std::strtoul(e.args[1].c_str(), nullptr, 16));
        char sz = e.args.size() > 2 ? e.args[2][0] : 'b';
        if (sz == 'l') machine->w32(a, v);
        else if (sz == 'w') machine->w16(a, uint16_t(v));
        else machine->w8(a, uint8_t(v));
      } else if (e.cmd == "poke" && e.args.size() >= 2) {
        // poke <hexaddr> <hexvalue> [b|w|l]: force a game variable (test
        // scenarios use it to reach states that are hard to play into)
        uint32_t a = uint32_t(std::strtoul(e.args[0].c_str(), nullptr, 16));
        uint32_t v = uint32_t(std::strtoul(e.args[1].c_str(), nullptr, 16));
        char sz = e.args.size() > 2 ? e.args[2][0] : 'b';
        if (sz == 'l') machine->w32(a, v);
        else if (sz == 'w') machine->w16(a, uint16_t(v));
        else machine->w8(a, uint8_t(v));
      } else if (e.cmd == "shot" && !e.args.empty()) {
        pending_shot = e.args[0];
      } else if (e.cmd == "quit") {
        quit = true;
      }
    }
  }

  std::string pending_shot;

  void save_screenshot(const uint32_t* fb) {
    FILE* f = fopen(screenshot_path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", amiga::FB_W, amiga::VIEW_H);
    for (int i = 0; i < amiga::FB_W * amiga::VIEW_H; ++i) {
      uint8_t px[3] = {uint8_t(fb[i] >> 16), uint8_t(fb[i] >> 8), uint8_t(fb[i])};
      fwrite(px, 1, 3, f);
    }
    fclose(f);
  }

  FILE* wav = nullptr;
  uint32_t wav_bytes = 0;

  void open_wav(const std::string& path) {
    wav = fopen(path.c_str(), "wb");
    if (wav) { uint8_t hdr[44] = {}; fwrite(hdr, 1, 44, wav); }
  }

  void close_wav() {
    if (!wav) return;
    auto le32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) fputc(int(v >> (8 * i)) & 0xFF, wav); };
    auto le16 = [&](uint16_t v) { fputc(v & 0xFF, wav); fputc(v >> 8, wav); };
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav); le32(36 + wav_bytes); fwrite("WAVEfmt ", 1, 8, wav);
    le32(16); le16(1); le16(2); le32(48000); le32(48000 * 4); le16(4); le16(16);
    fwrite("data", 1, 4, wav); le32(wav_bytes);
    fclose(wav);
    wav = nullptr;
  }

  void audio(const int16_t* samples, size_t frames) override {
    if (wav) { fwrite(samples, 4, frames, wav); wav_bytes += uint32_t(frames * 4); }
    if (!audio_dev || fast) return;
    // Keep latency bounded.
    if (SDL_GetQueuedAudioSize(audio_dev) > 48000 * 4 / 5) return;
    SDL_QueueAudio(audio_dev, samples, uint32_t(frames * 4));
  }
};

void usage() {
  fprintf(stderr,
          "usage: hostages [options] [disk.adf]\n"
          "  disk.adf         the game disk: \"Hostages\" (AmigaDOS) or \"Hostage\"\n"
          "                   (trackloader); default search order:\n"
          "                   \"Hostages (1988)(Infogrames)[cr].adf\",\n"
          "                   \"Hostages (1988)(Infogrames).adf\",\n"
          "                   \"Hostage (Infogrames).adf\" in ., assets/ or the\n"
          "                   install data dir\n"
          "  --scale N        window scale (default 3)\n"
          "  --save DIR       directory for the written disk (game saves)\n"
          "                   (default: $XDG_DATA_HOME/hostages)\n"
          "  --speed X        CPU speed multiplier (default 1 = A500)\n"
          "  --verbose        log OS calls\n"
          "  --trace          trace every instruction (very slow)\n"
          "  --screenshot N FILE  save frame N as PPM\n"
          "  --frames N       quit after N frames\n"
          "  --fast           run unthrottled\n"
          "  --protection     keep the manual word check\n"
          "  --skip-codes     accept any rank access code (Captain/Commander)\n"
          "  --wav FILE       record audio\n"
          "  --coverage FILE  write executed code addresses\n"
          "  --ram-hash N     log a RAM hash every N frames\n"
          "  --script FILE    scripted input (see source)\n"
          "  --grab           capture the mouse at start (F12 toggles)\n"
          "keys: cursor keys/right Ctrl = joystick, F11 fullscreen,\n"
          "      Pause fast-forward, F9 debug menu; all other keys to game.\n"
          "pad:  stick/D-pad = joystick, A = fire, B = Esc, X = Return,\n"
          "      Y = team select, Back = debug menu, Start/X = Return,\n"
          "      L/R shoulder = F1/F2; restart (F10) via team menu.\n");
}

}  // namespace

int main(int argc, char** argv) {
  std::string adf_path, save_dir;
  int scale = 3;
  double speed = 1.0;
  bool verbose = false, trace = false, fast = false, protection = false, skip_codes = false;
  int shot_frame = -1, quit_after = -1;
  std::string shot_path, script_path, wav_path, coverage_path;
  int ram_hash = 0;
  bool grab_mouse = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--scale" && i + 1 < argc) scale = std::atoi(argv[++i]);
    else if (a == "--save" && i + 1 < argc) save_dir = argv[++i];
    else if (a == "--speed" && i + 1 < argc) speed = std::atof(argv[++i]);
    else if (a == "--verbose") verbose = true;
    else if (a == "--trace") trace = true;
    else if (a == "--screenshot" && i + 2 < argc) { shot_frame = std::atoi(argv[++i]); shot_path = argv[++i]; }
    else if (a == "--frames" && i + 1 < argc) quit_after = std::atoi(argv[++i]);
    else if (a == "--fast") fast = true;
    else if (a == "--protection") protection = true;
    else if (a == "--skip-codes") skip_codes = true;
    else if (a == "--wav" && i + 1 < argc) wav_path = argv[++i];
    else if (a == "--coverage" && i + 1 < argc) coverage_path = argv[++i];
    else if (a == "--ram-hash" && i + 1 < argc) ram_hash = std::atoi(argv[++i]);
    else if (a == "--script" && i + 1 < argc) script_path = argv[++i];
    else if (a == "--grab") grab_mouse = true;
    else if (a == "-h" || a == "--help") { usage(); return 0; }
    else adf_path = a;
  }
  if (adf_path.empty()) {
    // Prefer the European AmigaDOS release; fall back to the trackloader disk.
    for (std::string cand : {std::string("."), std::string("assets"), std::string(HOSTAGES_DATADIR)}) {
      for (const char* name :
           {"Hostages (1988)(Infogrames)[cr].adf", "Hostages (1988)(Infogrames).adf",
            "Hostage (Infogrames).adf"}) {
        std::string p = cand + "/" + name;
        if (std::filesystem::exists(p)) { adf_path = p; break; }
      }
      if (!adf_path.empty()) break;
    }
  }
  if (adf_path.empty()) {
    fprintf(stderr, "Cannot find the game disk.\n");
    usage();
    return 1;
  }
  if (save_dir.empty()) {
    const char* xdg = getenv("XDG_DATA_HOME");
    const char* home = getenv("HOME");
    if (xdg && *xdg) save_dir = std::string(xdg) + "/hostages";
    else if (home) save_dir = std::string(home) + "/.local/share/hostages";
    else save_dir = ".";
  }

  amiga::Machine machine;
  machine.verbose = verbose;
  machine.trace = trace;
  machine.set_cpu_speed(speed);
  machine.coverage_path = coverage_path;
  machine.ram_hash_every = ram_hash;
  machine.bypass_protection = !protection;
  machine.skip_access_codes = skip_codes;
  // Disk writes go to a copy named after the ADF (one per release).
  std::string save_name = std::filesystem::path(adf_path).filename().string();
  if (!machine.boot(adf_path, save_dir + "/" + save_name)) return 1;

  SdlHost host;
  host.machine = &machine;
  host.save_dir = save_dir;
  host.screenshot_frame = shot_frame;
  host.screenshot_path = shot_path;
  host.quit_after = quit_after;
  host.fast = fast;
  if (!script_path.empty()) host.script = load_script(script_path);
  if (!host.init(scale)) return 1;
  if (grab_mouse) host.set_grab(true);
  machine.set_host(&host);
  if (!wav_path.empty()) host.open_wav(wav_path);
  machine.run();
  host.close_wav();
  SDL_Quit();
  return 0;
}
