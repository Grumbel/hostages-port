# Hostages – Amiga port to C++/SDL2

Port of the Amiga version of **Hostages** (Infogrames, 1989; also known as
*Operation Jupiter* / *Embassy Mission*) to C++ and SDL2.

The game's original 68000 code runs on a small runtime that provides
exactly what the code expects from an Amiga 500, so no Kickstart ROM and no
extracted files are needed – the game boots straight from the original disk
image:

- the parts of Kickstart the game uses, emulated at the library level:
  exec (memory, lists, ports, signals, interrupt servers, devices),
  trackdisk.device on the ADF, a few graphics.library drawing calls
  (InitRastPort/InitBitMap/RectFill/Move/Draw…) and the boot strap that runs
  the bootblock;
- the custom chips the game programs directly: copper and bitplane display,
  sprites, blitter, Paula audio DMA, disk DMA, the two CIAs (keyboard,
  joystick fire, music timer, drive control).

The manual word check ("enter word N on line N of page N") is bypassed by
default, much like a WHDLoad install patch: any answer, including just
Return, is accepted.

## Requirements

The original disk image `Hostage (Infogrames).adf` (not included). The game
looks for it in the current directory, `assets/` and the install data dir,
or it can be given on the command line.

## Build

```bash
nix develop          # or: install cmake, a C++20 compiler and SDL2
cmake -B build
cmake --build build
./build/hostages
```

or `nix build && ./result/bin/hostages "Hostage (Infogrames).adf"`.

## Running

```
hostages [options] [disk.adf]
  --scale N        window scale (default 3)
  --save DIR       directory for the written disk (default $XDG_DATA_HOME/hostages)
  --speed X        CPU speed multiplier (default 1 = A500)
  --protection     keep the manual word check
  --fast           run unthrottled
```

Developer options: `--verbose` (log OS calls), `--trace`, `--frames N`,
`--screenshot N file.ppm`, `--script file`, `--wav file`, `--coverage file`,
`--ram-hash N` (see `hostages --help` and AGENTS.md).

## Controls

The game is played with a joystick in port 2 and the keyboard:

| Input | Function |
|-------|----------|
| Cursor keys / game controller stick | joystick (the game also reads the cursor keys itself) |
| Right Ctrl / Space / controller A | fire |
| Return | confirm / next screen |
| F1–F10, other keys | passed to the game as on the Amiga (e.g. F4–F6 select team members in part II, F10 restart) |
| F11 | fullscreen |
| Pause | fast forward |

## License

GPLv3+ for the new code. The game itself is not included and not
redistributable; this is a reverse-engineering / preservation project.
