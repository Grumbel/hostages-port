<!-- SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com> -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Hostages for the R36S (ArkOS) and other PortMaster handhelds

Port of the Amiga version of Hostages (Infogrames, 1988). Native aarch64
build, linked against an ArkOS-era (Ubuntu 19.10) userland; it uses the
device's SDL2.

## Install

Copy `Hostages.sh` and the `hostages/` folder to `/roms/ports/`, or put the
zip into `ports/PortMaster/autoinstall/` and start PortMaster once.

## Disk image

The game disk is not included and may not be redistributed. Copy your own
Amiga disk image into `ports/hostages/`, e.g.
`Hostages (1988)(Infogrames)[cr].adf` (AmigaDOS release) or
`Hostage (Infogrames).adf` (trackloader release); any other `*.adf` in that
folder is used too.

## Controls

| Button       | Action                          |
|--------------|---------------------------------|
| D-pad, stick | move                            |
| A            | fire                            |
| B            | Esc (map / gameplay screen)     |
| X, Start     | Return                          |
| Y            | team select                     |
| L1 / R1      | F1 / F2                         |
| Select       | debug menu (save/load states)   |
| Select+Start | quit                            |

Saves and the written disk are kept in `hostages/conf/`.

Source and licence (GPL-3.0-or-later): https://github.com/Grumbel/hostages-port
