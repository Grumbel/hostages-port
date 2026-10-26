# SPDX-FileCopyrightText: 2025–2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# R36S / ArkOS aarch64 cross build against github:grumnix/arkos-sysroot.
#
#   nix build .#hostages-r36s
#   nix build .#hostages-r36s-portmaster
#   nix build .#hostages-r36s-portmaster-zip

{ lib
, stdenv
, stdenvNoCC
, cmake
, pkg-config
, pkgsCross
, writeShellScript
, zip
, sysrootSrc
, sysrootOverride ? null
}:

let
  arkosSysroot = if sysrootOverride != null then sysrootOverride else stdenvNoCC.mkDerivation {
    pname = "arkos-sysroot";
    version = "0.1";
    src = sysrootSrc;
    dontConfigure = true;
    dontBuild = true;
    dontFixup = true;
    dontPatchELF = true;
    dontStrip = true;
    dontPatchShebangs = true;
    dontCheckForBrokenSymlinks = true;
    installPhase = ''
      runHook preInstall
      mkdir -p "$out"
      cp -a . "$out/"
      runHook postInstall
    '';
  };

  mkHostagesR36s = {
    src
  , version
  }:
    pkgsCross.aarch64-multiplatform.stdenv.mkDerivation {
      pname = "hostages-r36s";
      inherit version src;

      nativeBuildInputs = [ cmake pkg-config ];

      # Link against sysroot SDL2; do not use nixpkgs' modern glibc.
      cmakeFlags = [
        "-DCMAKE_BUILD_TYPE=Release"
        "-DPROJECT_VERSION_FULL=${version}"
      ];

      dontPatchELF = true;
      dontStrip = true;
      dontPatchShebangs = true;

      preConfigure = ''
        export NIX_DONT_SET_RPATH=1
        export NIX_NO_SELF_RPATH=1
        export PKG_CONFIG="pkg-config"
        export PKG_CONFIG_SYSROOT_DIR="${arkosSysroot}"
        export PKG_CONFIG_PATH=""
        export PKG_CONFIG_LIBDIR="${arkosSysroot}/usr/lib/aarch64-linux-gnu/pkgconfig:${arkosSysroot}/usr/lib/pkgconfig:${arkosSysroot}/usr/share/pkgconfig"
        export CMAKE_PREFIX_PATH="${arkosSysroot}/usr"
        export CMAKE_SYSROOT="${arkosSysroot}"
        # Prefer sysroot headers/libs
        export CFLAGS="--sysroot=${arkosSysroot}"
        export CXXFLAGS="--sysroot=${arkosSysroot}"
        export LDFLAGS="--sysroot=${arkosSysroot}"
        pkg-config --modversion sdl2 || { echo "arkos-sysroot: sdl2.pc not found" >&2; exit 1; }
      '';

      meta = with lib; {
        description = "hostages for R36S/ArkOS (sysroot-linked aarch64)";
        license = licenses.gpl3Plus;
        platforms = platforms.linux;
        hydraPlatforms = [];
      };
    };

  mkHostagesR36sPortMaster = {
    r36sPkg
  , version
  , pname ? "hostages-r36s-portmaster"
  , title ? "Hostages"
  , scriptName ? "Hostages.sh"
  , portDirName ? "hostages"
  }:
    stdenvNoCC.mkDerivation {
      inherit pname version;
      dontUnpack = true;
      dontConfigure = true;
      dontBuild = true;
      dontFixup = true;
      dontPatchShebangs = true;

      installPhase = ''
        set -euo pipefail
        root="$out"
        mkdir -p "$root/${portDirName}"

        cp ${r36sPkg}/bin/hostages "$root/${portDirName}/hostages"
        chmod +x "$root/${portDirName}/hostages"
        if [ -f ${../data/icons/hicolor/scalable/apps/hostages.svg} ]; then
          cp ${../data/icons/hicolor/scalable/apps/hostages.svg} "$root/${portDirName}/cover.svg"
        fi

        cat > "$root/${scriptName}" << 'EOF_SH'
#!/bin/bash
# Hostages — PortMaster launcher for R36S / ArkOS
if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi
source "$controlfolder/control.txt" 2>/dev/null || true
get_controls 2>/dev/null || true
GAMEDIR="$(cd "$(dirname "$0")" && pwd)/hostages"
cd "$GAMEDIR" || exit 1
# User-supplied ADF expected in this directory or via argument.
export XDG_STATE_HOME="''${XDG_STATE_HOME:-$GAMEDIR/saves}"
mkdir -p "$XDG_STATE_HOME"
$GPTOKEYB "hostages" -c "$GAMEDIR/hostages.gptk" &
./hostages "$@"
kill -9 $GPTOKEYB 2>/dev/null || true
EOF_SH
        chmod +x "$root/${scriptName}"

        cat > "$root/${portDirName}/hostages.gptk" << 'EOF_GPTK'
# Optional gptokeyb mapping; in-game uses SDL gamecontroller.
EOF_GPTK

        cat > "$root/port.json" << EOF_JSON
{
  "version": 2,
  "name": "${scriptName}",
  "items": ["${scriptName}", "${portDirName}/"],
  "items_opt": [],
  "attr": {
    "title": "${title}",
    "desc": "Amiga Hostages (Infogrames) port. Requires a user-supplied ADF disk image.",
    "inst": "Copy Hostages.sh and hostages/ into /roms/ports/. Place an ADF in hostages/.",
    "genres": ["action", "arcade"],
    "porter": ["Ingo Ruhnke"],
    "image": {},
    "rtr": false,
    "runtime": null,
    "reqs": [],
    "arch": ["aarch64"]
  }
}
EOF_JSON

        cat > "$root/README.md" << 'EOF_README'
## Hostages (R36S / ArkOS)

Native aarch64 build linked against the ArkOS sysroot (SDL2).

### Install
Copy `Hostages.sh` and the `hostages/` directory to `/roms/ports/`.

### Disk image
Place a non-redistributable ADF (e.g. `Hostage (Infogrames).adf`) in the
`hostages/` folder. Saves go under `hostages/saves/` by default.

### Controls
Gamepad: stick/D-pad move, A fire, B Esc, Y team menu, Back debug menu,
Start/X confirm. See project README for full mapping.
EOF_README
      '';

      meta = with lib; {
        description = "PortMaster package of hostages for R36S/ArkOS";
        license = licenses.gpl3Plus;
        platforms = platforms.linux;
        hydraPlatforms = [];
      };
    };

  mkHostagesR36sPortMasterZip = {
    portMasterPkg
  , version
  , pname ? "hostages-r36s-portmaster-zip"
  , zipName ? "hostages.zip"
  }:
    stdenvNoCC.mkDerivation {
      inherit pname version;
      dontUnpack = true;
      dontConfigure = true;
      dontBuild = true;
      dontFixup = true;
      dontPatchShebangs = true;
      nativeBuildInputs = [ zip ];
      installPhase = ''
        set -euo pipefail
        mkdir -p "$out"
        ( cd "${portMasterPkg}" && zip -r -9 "$out/${zipName}" . )
      '';
      meta = with lib; {
        description = "PortMaster autoinstall zip of hostages for R36S/ArkOS";
        license = licenses.gpl3Plus;
        platforms = platforms.linux;
        hydraPlatforms = [];
      };
    };
in
{
  inherit arkosSysroot mkHostagesR36s mkHostagesR36sPortMaster mkHostagesR36sPortMasterZip;
}
