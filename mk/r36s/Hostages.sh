#!/bin/bash
# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Hostages: PortMaster launcher for the R36S / ArkOS (and alike).

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi
source "$controlfolder/control.txt"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/hostages"
cd "$GAMEDIR" || exit 1
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

# The disk image is not redistributable: the user copies it into this
# folder. The usual TOSEC names are preferred, then any other *.adf.
adf=
for f in "Hostages (1988)(Infogrames)[cr].adf" "Hostages (1988)(Infogrames).adf" \
         "Hostage (Infogrames).adf" *.adf *.ADF; do
  if [ -f "$f" ]; then adf="$f"; break; fi
done
if [ -z "$adf" ]; then
  echo "No Hostages disk image found: copy the *.adf into $GAMEDIR"
  sleep 5
  exit 1
fi

# Saves and the written disk stay with the port (not under ~/.local — SD card only).
mkdir -p "$GAMEDIR/conf"
export XDG_STATE_HOME="$GAMEDIR/conf"
# The pad's layout from PortMaster, for SDL's game controller API.
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

# gptokeyb: Select+Start quits.
$GPTOKEYB "hostages" &
pm_platform_helper "$GAMEDIR/hostages"
./hostages "$adf"
pm_finish
