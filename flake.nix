# SPDX-FileCopyrightText: 2025–2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

{
  description = "Hostages (Infogrames) – C++/SDL2 port of the Amiga version";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";

    SDL2-win32.url = "github:grumnix/SDL2-win32";
    SDL2-win32.inputs.nixpkgs.follows = "nixpkgs";
    SDL2-win32.inputs.flake-utils.follows = "flake-utils";

    sdl2-src = {
      url = "https://github.com/libsdl-org/SDL/releases/download/release-2.30.3/SDL2-2.30.3.tar.gz";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, flake-utils, SDL2-win32, sdl2-src }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config = {
            allowUnfree = true;
            android_sdk.accept_license = true;
          };
        };
        lib = pkgs.lib;

        versionBase = lib.strings.fileContents ./VERSION;
        gitRev = "${self.shortRev or self.dirtyShortRev or "dirty"}";
        isDev = lib.strings.hasInfix "-dev" versionBase;
        version =
          if isDev then
            "${versionBase}.${toString (self.revCount or 0)}+g${gitRev}"
          else
            versionBase;

        hostages = pkgs.stdenv.mkDerivation {
          pname = "hostages";
          inherit version;
          src = lib.cleanSource ./.;

          nativeBuildInputs = with pkgs; [ cmake pkg-config ];
          buildInputs = with pkgs; [ SDL2 ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
            "-DPROJECT_VERSION_FULL=${version}"
          ];

          dontStrip = true;
          separateDebugInfo = false;

          meta = with lib; {
            mainProgram = "hostages";
            description = "Port of the Amiga version of Hostages (Infogrames, 1988)";
            homepage = "https://github.com/Grumbel/hostages-port";
            license = licenses.gpl3Plus;
            platforms = platforms.linux;
          };
        };

        windows = import ./nix/windows.nix {
          inherit pkgs version;
          sdl2Win64 = SDL2-win32.packages.${system}.SDL2-win64;
          sdl2Win32 = SDL2-win32.packages.${system}.SDL2-win32;
        };

        wasm = import ./nix/wasm.nix {
          inherit pkgs version gitRev;
          sdlSrc = sdl2-src;
          sdlVersion = "2.30.3";
        };

        # R36S / ArkOS: eoan sysroot, see nix/r36s.nix.
        r36s = import ./nix/r36s.nix { inherit pkgs version; };

        androidSdk = (pkgs.androidenv.composeAndroidPackages {
          cmdLineToolsVersion = "11.0";
          toolsVersion = "26.1.1";
          platformToolsVersion = "34.0.5";
          buildToolsVersions = [ "34.0.0" ];
          includeEmulator = false;
          platformVersions = [ "34" "21" ];
          includeSources = false;
          includeSystemImages = false;
          includeNDK = true;
          ndkVersions = [ "26.3.11579264" ];
          useGoogleAPIs = false;
        }).androidsdk;

        android = import ./nix/android.nix {
          inherit pkgs version androidSdk;
          sdlSrc = sdl2-src;
          sdlVersion = "2.30.3";
          buildToolsVersion = "34.0.0";
          packagePlatform = "21";
          compilePlatform = "34";
          targetAbis = [ "armeabi-v7a" "arm64-v8a" ];
          versionCode = 1;
        };

      in
      {
        packages = {
          default = hostages;
          hostages = hostages;

          hostages-win64 = windows.hostages-win64;
          hostages-win32 = windows.hostages-win32;
          hostages-win64-zip = windows.hostages-win64-zip;
          hostages-win32-zip = windows.hostages-win32-zip;

          hostages-wasm = wasm.hostagesWasm;
          sdl2-wasm = wasm.sdl2Wasm;

          arkos-sysroot = r36s.sysroot;
          hostages-r36s = r36s.r36s;
          hostages-r36s-portmaster = r36s.portmaster;
          hostages-r36s-portmaster-zip = r36s.portmasterZip;

          hostages-android = android.apk;
        };

        apps = {
          default = {
            type = "app";
            program = "${hostages}/bin/hostages";
          };
          hostages-wasm = wasm.serveApp;
          hostages-android-install = android.installApp;
        };

        checks.version = pkgs.runCommand "hostages-version" {
          nativeBuildInputs = [ hostages ];
        } ''
          outv=$(hostages --version)
          echo "$outv" | grep -F "${version}"
          hostages --help >/dev/null
          hostages --show-paths >/dev/null || true
          test -f ${hostages}/share/applications/hostages.desktop
          test -f ${hostages}/share/metainfo/org.grumbel.Hostages.metainfo.xml
          touch $out
        '';

        devShells.default = pkgs.mkShell {
          inputsFrom = [ hostages ];
          packages = with pkgs; [
            cmake
            pkg-config
            gdb
            python3
            xxd
            file
            pkgsCross.m68k.buildPackages.binutils
            (writeShellScriptBin "hostages-configure" ''
              set -euo pipefail
              if [ -z "''${PROJECT_SOURCE:-}" ]; then
                echo "$0: PROJECT_SOURCE is not set (enter the shell with: nix develop)" >&2
                exit 1
              fi
              PROJECT_BUILD_DIR="''${PROJECT_BUILD_DIR:-/tmp/hostages-build}"
              mkdir -p "$PROJECT_BUILD_DIR"
              cd "$PROJECT_BUILD_DIR"
              extra=()
              if [ -n "''${HOSTAGES_VERSION_FULL:-}" ]; then
                extra+=("-DPROJECT_VERSION_FULL=$HOSTAGES_VERSION_FULL")
              fi
              cmake \
                -DCMAKE_BUILD_TYPE="''${CMAKE_BUILD_TYPE:-Debug}" \
                -DCMAKE_CXX_FLAGS_DEBUG="-O0 -g" \
                -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O2 -g -DNDEBUG" \
                -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
                "''${extra[@]}" \
                "$PROJECT_SOURCE"
            '')
            (writeShellScriptBin "hostages-build" ''
              set -euo pipefail
              if [ -z "''${PROJECT_SOURCE:-}" ]; then
                echo "$0: PROJECT_SOURCE is not set (enter the shell with: nix develop)" >&2
                exit 1
              fi
              PROJECT_BUILD_DIR="''${PROJECT_BUILD_DIR:-/tmp/hostages-build}"
              if [ ! -f "$PROJECT_BUILD_DIR/CMakeCache.txt" ]; then
                hostages-configure
              else
                cached=$(grep -E '^CMAKE_HOME_DIRECTORY:' "$PROJECT_BUILD_DIR/CMakeCache.txt" | cut -d= -f2 || true)
                if [ "$cached" != "$PROJECT_SOURCE" ]; then
                  echo "Source path changed ($cached -> $PROJECT_SOURCE), reconfiguring..."
                  hostages-configure
                fi
              fi
              cmake --build "$PROJECT_BUILD_DIR" -j"$(nproc)"
            '')
            (writeShellScriptBin "hostages-run" ''
              set -euo pipefail
              if [ -z "''${PROJECT_SOURCE:-}" ]; then
                echo "$0: PROJECT_SOURCE is not set (enter the shell with: nix develop)" >&2
                exit 1
              fi
              hostages-build
              PROJECT_BUILD_DIR="''${PROJECT_BUILD_DIR:-/tmp/hostages-build}"
              exec "$PROJECT_BUILD_DIR/hostages" "$@"
            '')
            (writeShellScriptBin "hostages-run-gdb" ''
              set -euo pipefail
              if [ -z "''${PROJECT_SOURCE:-}" ]; then
                echo "$0: PROJECT_SOURCE is not set (enter the shell with: nix develop)" >&2
                exit 1
              fi
              hostages-build
              PROJECT_BUILD_DIR="''${PROJECT_BUILD_DIR:-/tmp/hostages-build}"
              exec gdb -q \
                -ex "set pagination off" \
                -ex "set confirm off" \
                -ex "set debuginfod enabled off" \
                -ex run \
                -ex 'python
try:
  ec = gdb.parse_and_eval("$_exitcode")
  if int(ec) == 0:
    gdb.execute("quit")
except Exception:
  pass
' \
                --args "$PROJECT_BUILD_DIR/hostages" "$@"
            '')
          ];
          CMAKE_BUILD_TYPE = "Debug";
          shellHook = ''
            export PROJECT_SOURCE="$PWD"
            export PROJECT_BUILD_DIR="''${PROJECT_BUILD_DIR:-/tmp/hostages-build}"
            export HOSTAGES_VERSION_FULL="${version}"
            echo "Hostages develop shell (version ${version})"
            echo "  hostages-configure | hostages-build | hostages-run | hostages-run-gdb"
            echo "  packages: hostages hostages-win64 hostages-wasm hostages-r36s hostages-android"
          '';
        };
      });
}
