# SPDX-FileCopyrightText: 2025–2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

# Windows builds, cross-compiled with MinGW. SDL2 is the official prebuilt
# MinGW package (SDL2-win32 flake).
#
#   nix build .#hostages-win64
#   nix build .#hostages-win64-zip

{ pkgs
, sdl2Win64
, sdl2Win32
, version
}:

let
  lib = pkgs.lib;

  mkGame = { crossPkgs, sdl2, arch }:
    crossPkgs.stdenv.mkDerivation {
      pname = "hostages-${arch}-build";
      inherit version;
      src = lib.cleanSource ../.;
      nativeBuildInputs = [ pkgs.cmake ];
      buildInputs = [ sdl2 crossPkgs.windows.mcfgthreads ];
      cmakeFlags = [
        "-DCMAKE_BUILD_TYPE=Release"
        "-DPROJECT_VERSION_FULL=${version}"
        "-DSDL2_DIR=${sdl2}/lib/cmake/SDL2"
      ];
    };

  mkFolder = { crossPkgs, sdl2, arch }:
    let game = mkGame { inherit crossPkgs sdl2 arch; };
    in pkgs.runCommand "hostages-${arch}-${version}" { } ''
      mkdir -p $out
      cp ${game}/bin/hostages.exe $out/
      cp ${sdl2}/bin/SDL2.dll $out/
      cp ${crossPkgs.windows.mcfgthreads}/bin/libmcfgthread-[0-9]*.dll $out/ || true
      cp ${../LICENSES/GPL-3.0-or-later.txt} $out/COPYING.txt
      cat > $out/README.txt <<EOR
Hostages ${version}
Port of the Amiga version of Hostages (Infogrames, 1988).

Place a non-redistributable ADF next to hostages.exe or pass it on the
command line. Saves and the written disk go under
%APPDATA%\\grumbel\\hostages\\ (SDL pref path).

Licence: GPL-3.0-or-later (COPYING.txt).
Source: https://github.com/Grumbel/hostages-port
EOR
      sed -i 's/$/\r/' $out/README.txt
    '';

  mkZip = { folder, arch }:
    pkgs.runCommand "hostages-${version}-${arch}.zip" { nativeBuildInputs = [ pkgs.zip ]; } ''
      mkdir -p $out
      work=$(mktemp -d)
      cp -r --no-preserve=mode ${folder} "$work/hostages-${version}-${arch}"
      cd "$work"
      zip -r "$out/hostages-${version}-${arch}.zip" "hostages-${version}-${arch}"
    '';

  win64Folder = mkFolder {
    crossPkgs = pkgs.pkgsCross.mingwW64;
    sdl2 = sdl2Win64;
    arch = "win64";
  };
  win32Folder = mkFolder {
    crossPkgs = pkgs.pkgsCross.mingw32;
    sdl2 = sdl2Win32;
    arch = "win32";
  };
in {
  hostages-win64 = win64Folder;
  hostages-win32 = win32Folder;
  hostages-win64-zip = mkZip { folder = win64Folder; arch = "win64"; };
  hostages-win32-zip = mkZip { folder = win32Folder; arch = "win32"; };
}
