# Hostages – Amiga port to C++/SDL2

Port of the Amiga version of **Hostages** (Infogrames, 1989; also known as
*Operation Jupiter* / *Embassy Mission*) to C++ and SDL2.

The game's original 68000 code runs on a small runtime that provides
exactly what the code expects from an Amiga 500, so no Kickstart ROM and no
extracted files are needed – the game boots straight from the original disk
image. Two releases are supported:

- **Hostage** (`Hostage (Infogrames).adf`): Infogrames' trackloader disk
  (custom bootblock, own file directory, manual word check, rank access
  codes);
- **Hostages** (`Hostages (1988)(Infogrames)[cr].adf`): the European release
  as a cracked AmigaDOS disk (startup-sequence runs `apro1.` and then
  `it.`, an Intuition screen, no checks). It has the "HOSTAGES" logo, the
  mission "JUPITER" instead of "ASSAULT" and different intro music.

What is emulated:

- the parts of Kickstart the game uses, emulated at the library level:
  exec (memory with real MemHeaders, lists, ports, signals, interrupt
  servers, devices), trackdisk.device on the ADF, the boot strap that runs
  the bootblock, or for an AmigaDOS disk a CLI that executes
  s/startup-sequence, dos.library on the disk's OFS file system (Open/Read/
  Lock/Examine/LoadSeg…), Intuition screens/windows and the graphics.library
  calls used (RastPort drawing, BltBitMap, LoadRGB4, AllocRaster…);
- the custom chips the game programs directly: copper and bitplane display,
  sprites, blitter, Paula audio DMA, disk DMA, the two CIAs (keyboard,
  joystick fire, music timer, drive control).

The manual word check of "Hostage" ("enter word N on line N of page N") is
bypassed by default, much like a WHDLoad install patch: any answer,
including just Return, is accepted. `--skip-codes` also skips the secret
access codes for the CAPTAIN and COMMANDER ranks (the end screens give
them: HPETS for Captain, EDUAB for Commander); "Hostages" has no codes.

## Requirements

One of the two disk images (not included). The game looks for them in the
current directory, `assets/` and the install data dir, or the ADF can be
given on the command line.

## Support matrix

| Target | Status | Notes |
|--------|--------|-------|
| Linux x86_64 / aarch64 (SDL2) | Supported | Primary development and packaging target |
| Windows / WASM / Android / R36S | N/A | Not declared; ADF + full machine state make handheld/web packaging non-goals for now |

Version source: top-level `VERSION` (`0.1.0-dev` in tree). Nix development builds append `.<revCount>+g<rev>`.

## Build

```bash
nix develop
hostages-configure && hostages-build
hostages-run [disk.adf]          # or: hostages-run-gdb
# in-tree still works:
cmake -B build && cmake --build build
./build/hostages
```

Package / one-shot:

```bash
nix build            # result/bin/hostages
nix build .#checks.version
nix run . -- --version
```

Requires one of the two non-redistributable ADF images (see above).

## Running

```
hostages [options] [disk.adf]
  --scale N        window scale (default 3)
  --save DIR       written disk + save states (default $XDG_STATE_HOME/hostages;
                   falls back to $XDG_DATA_HOME/hostages if present)
  -V, --version    print version
  --show-paths     print resolved paths and exit
  --speed X        CPU speed multiplier (default 1 = A500)
  --protection     keep the manual word check
  --skip-codes     accept any rank access code
  --fast           run unthrottled
```

Developer options: `--verbose` (log OS calls), `--trace`, `--frames N`,
`--screenshot N file.ppm`, `--script file`, `--wav file`, `--coverage file`,
`--ram-hash N` (see `hostages --help` and AGENTS.md).

## Controls

The game is played with a joystick in port 2, the keyboard, or a gamepad:

| Input | Function |
|-------|----------|
| Cursor keys / game controller stick / D-pad | joystick (the game also reads the cursor keys itself) |
| Right Ctrl / Space / controller A | fire |
| Return / controller X or Start | confirm / next screen / continue |
| F1–F6 | select team member (DELTA…BRAVO); on a gamepad use **Y (North)** to open the team menu instead |
| F10 | restart current stage (on gamepad: team menu → Restart when stage I targets are set) |
| F11 | fullscreen |
| Pause | fast forward |
| F9 / controller Back | debug menu (save states, stage I complete, …) |
| Controller Y (North) | team select menu (stage I: DELTA/ECHO/MIKE; stage II: all six; when stage I targets are set also Continue / Restart) |
| Controller B (East) | Esc (stage II map / gameplay screen toggle) |
| L/R shoulder | F1 / F2 |

## License

GPLv3+ for the new code. The game itself is not included and not
redistributable; this is a reverse-engineering / preservation project.
