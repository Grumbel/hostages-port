# Hostages Port

## Goal
Port the Amiga version of Hostages (Infogrames, 1988/89) to modern C++ with
SDL2. Source material: `Hostage (Infogrames).adf` (in git) and
`Hostages (1988)(Infogrames)[cr].adf` (European release, cracked AmigaDOS
disk; not in git). Both are not redistributable.

## Route
The game is an Atari ST port that drives the Amiga hardware directly
(copper, blitter, Paula, CIAs); it makes almost no OS calls. So the HLE
boundary is the chipset, not graphics.library: the original 68000 code runs
in an interpreter (`src/m68k`, taken from the FA-18 Interceptor port and
validated against the SingleStepTests suite), the custom chips are
emulated at register level, and only exec/trackdisk.device/a few
graphics.library calls are emulated as high-level functions. The machine
boots the real bootblock from the ADF. Static recompilation and
hand-translation to C++ (as in ~/projects/fa18-interceptor) can be layered
on later: programs load at deterministic addresses (see below).

## Two releases
- "Hostage" (trackloader disk, below): Hostage.adf. Programs `apro1.` and
  `it.` are loaded by stage 2; manual check, access codes HPETS/EDUAB.
- "Hostages" (AmigaDOS OFS disk, extract with the FA-18 project's
  tools/adf.py): s/startup-sequence = `apro1.` / `stack 2000` /
  `load it. -s 3000` (LOAD = RunBackground) / `endcli`. Hle::boot_dos runs
  the script line by line (next_command when a program returns). Programs
  use dos.library for files, open an Intuition screen (320×200×4, custom
  bitmap) and window, walk exec's MemList ("Not enough memory" unless a
  free chunk ≥ 230000 bytes) and double-buffer by patching the plane
  pointers in GfxBase->ActiView->LOFCprList->start (searching for the
  BPL4PTL move from +64), so the screen copper list mimics MrgCop's layout
  (8 sprite pointers first). Part II restores COP1LC = GfxBase->copinit,
  which therefore jumps (COP2LC/COPJMP2) to the front screen's list.
  Part I leaves buffers allocated; with exec's priorities (expansion RAM
  before chip) part II still gets its chip RAM. No manual check, ranks
  freely selectable, mission "JUPITER", different gign.dum/gign.ins music.
  Stage-I positions: -7600(a4), 3 rows × 3 slots × word.

## Disk format of "Hostage" (tools/hdisk.py)
No AmigaDOS file system.
- `$000` bootblock: hunk loader; reads the executable at `$800` via the
  trackdisk IORequest strap passes in a1, returns its entry in a0.
- `$400` directory: 50 × {char name[12]; u32 offset; u32 size}.
- `tatou.` (`$800`) = stage 2: opens trackdisk.device, implements
  open/read/write/close on the directory, then loops forever:
  run("apro1."), run("it."). run() loads the hunk file with AllocMem
  (+8 byte header), saves a4 at data+$226, `jsr (a6)` at code offset $256
  (hooked by the port: TRAP_RUN_PROGRAM → Machine::program_loaded).
- `apro1.` = part I: intro, manual check, rank/mission menu, briefing,
  plan + street (sniper) phase, then GARDE picture + demo (`seq.dem`).
- `it.` = part II: assault on the embassy, ending (`final.pc1`).
- Data files: `.pc1` Degas Elite compressed pictures, `.cpl` compressed
  graphics, `.dum`/`.ins` music + instruments, etc.

## Known facts
- Programs: Lattice-style C, small data model (a4 = data hunk + $7FFE),
  startup pops its return address; library calls through stubs.
- Both programs run with their code hunk at $3998 and the data hunk right
  after it (deterministic first-fit allocator; chip heap starts at $2000).
  Requests without MEMF_CHIP go to the $C00000 expansion first (exec's
  MemHeader priorities); the stacks are there too.
- Part II needs ~470 KB chip RAM: stacks are therefore in slow RAM
  ($C00000), like Kickstart does on an A500 with a trapdoor expansion.
- Display: one copper list per program, 4 planes, 320×200 at lines
  $2C–$F4; sprite pointers point at an empty sprite (no sprites seen in
  use yet, but they are emulated).
- Input: a VBL hook polls CIA-A SDR once per frame (Kickstart's keyboard
  handler does the handshake; the port holds each key code in SDR for
  1.5 frames) and JOY1DAT + CIA-A PRA bit 7; cursor keys and Space are
  also mapped onto the joystick bits by the game. State byte
  -7054(a4) = up 1, down 2, left 4, right 8, fire $80; last key -7062(a4).
- Music: CIA-B timer A interrupt server on the EXTER chain
  (AddIntServer(13)); the server reads CIA-B ICR itself.
- Manual check: apro1 code offset $6958 `clr.b -2(a5)` (wrong answer) is
  NOP'd unless --protection (Machine::program_loaded patch table).
- Access codes (--skip-codes): apro1 $3F9C `bcc.w` → `bra.w` (skip the
  5-letter input loop) and $41AC `bne` → NOP (strcmp result ignored).
- Long-track protection (apro1 $6182: track 3 read with sync $A144, needs
  ≥13020 $AA bytes, installs a div-by-zero vector setting d2=$12345678)
  and its decryptor ($6384) are never called in this release. Disk DMA
  emulation still answers such a read like the original disk would.
- Higher ranks (CAPTAIN/COMMANDER) ask for the access codes from the end
  screens (HPETS, EDUAB) when a mission is chosen.
- apro1 has a write_file() ($6E94, mode 1006) that is never called; the
  disk write path (copy-on-write to the save dir) is therefore unused.
- Stage I ends when the player presses Return on the Esc menu/map once
  the marksmen are in position or dead (the ticker then says "Press Return
  to go on to the next screen or F10 to start again"). Internally it is
  done when every surviving marksman (records at -20902(a4),
  28 bytes, alive = +18) occupies a target position (words at -206(a4),
  14 bytes apart, value man+1); status function $1894. The scenario
  tests/scenarios/part2.txt pokes these to reach part II.
- The blitter must not be instantaneous: the spotlight collision blit
  writes BLTBMOD after BLTSIZE (docs/spotlight_collision.md). The blitter
  runs row by row in CPU time (Machine::blit_sync); HLE graphics calls
  that touch chip RAM through the CPU are unaffected.
- graphics.library calls used: InitRastPort, InitBitMap, SetAPen/BPen,
  SetDrMd, RectFill, Move/Draw, WritePixel, BltBitMap (helicopter scene),
  Own/DisownBlitter, WaitBlit.

## Tools
- `tools/hdisk.py list|extract ADF [dir]` — custom disk format
- `tools/hunks.py FILE` — hunk table
- `tools/disasm.py ADF PROGRAM [--base A]` — relocated disassembly at
  runtime addresses (m68k objdump from the nix shell)
- `tests/run_scenarios.sh` — scripted headless runs of
  tests/scenarios/<release>/*.txt with that release's ADF → PPM screenshots
  in build/scenarios/. Script lines: `<frame> key <name|hex> [down|up|tap]`,
  `joy udlrf`, `shot file.ppm`, `poke addr val [b|w|l]`, `pokea4 <offset>
  val [b|w|l]` (relative to the running program's a4), `quit` (frame may be
  `+N`); `# args: ...` adds command line options. Input timing during the
  intro changes its course (Return opens the menu in "Hostages").
- Debug menu (controller **Back** button or **F9**): pauses emulation and
  shows an overlay. Items: Resume, Save/Load state (slots 1–5 under the
  save dir `states/`), Slot selector, Complete stage I (pokes the marksmen
  target words for the active release), Soft reset, Screenshot, Fast
  forward toggle. Navigation: D-pad / cursor keys, A/Return to activate,
  Back/Esc/F9 to close. States are full machine snapshots (RAM, CPU,
  chips, CIAs, disk image, HLE).
- Debug environment variables: `HOSTAGES_COPDUMP=frame` (dump the copper
  list), `HOSTAGES_SPRDUMP=1` (sprite state every 50 frames),
  `HOSTAGES_WATCHB=hexaddr` (log changes of a byte per frame),
  `HOSTAGES_START_IT=1` (start with part II, on both releases; it lacks
  part I's state, so only useful for smoke tests), `HOSTAGES_BLITLOG=1` (log blitter register
  writes that happen while a blit is running, and every blit start),
  `HOSTAGES_WATCHPC=hexaddr` (log each change of a byte with the PC that
  made it; checked after every instruction), `HOSTAGES_AUDLOG=frame`
  (Paula register writes for three frames from `frame`).
- The intro logo reads "HOSTAGE" by design: it is drawn from two letter
  strings at -28727(a4) ("H S A E") and -28717(a4) ("O T G") by the
  falling-letters routine $4D8A; there is no second S in the data.
- Music: CIA-B timer A server ($B502, AddIntServer(13)); the game only
  writes TAHI = $19, so the period is $19FF+1 E-clocks (106.6 Hz) with
  the power-on latch low byte. $1299E is the player's re-entrancy flag.

## Build
```
nix develop
cmake -B build && cmake --build build
./build/hostages
```

## Workflow
- See TODO.md for status and next steps.
- Re-run `tests/run_scenarios.sh` after changes to the machine and look at
  the screenshots.
