# SPDX-FileCopyrightText: 2025–2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

# WebAssembly (Emscripten) build: static SDL2 offline, then the game HTML/JS/WASM.
#
#   nix build .#hostages-wasm
#   nix run .#hostages-wasm

{ pkgs
, sdlSrc
, sdlVersion
, version
, gitRev
}:

let
  emscripten = pkgs.emscripten;

  setupEmCache = name: ''
    export EM_CACHE="$TMPDIR/emcache-${name}"
    cp -r ${emscripten}/share/emscripten/cache "$EM_CACHE"
    chmod -R u+w "$EM_CACHE"
  '';

  sdl2Wasm = pkgs.stdenv.mkDerivation {
    pname = "sdl2-wasm";
    version = sdlVersion;
    dontUnpack = true;
    dontConfigure = true;
    dontUseCmakeConfigure = true;
    nativeBuildInputs = [ emscripten pkgs.cmake pkgs.python3 ];
    env.SDL_SRC = "${sdlSrc}";
    buildPhase = ''
      runHook preBuild
      ${setupEmCache "sdl2"}
      PREFIX=$PWD/prefix bash ${../mk/wasm/scripts/build-sdl2.sh}
      runHook postBuild
    '';
    installPhase = ''
      runHook preInstall
      mkdir -p $out
      cp -a prefix/. $out/
      runHook postInstall
    '';
    dontStrip = true;
    meta = {
      description = "Static SDL2 for wasm32-emscripten";
      license = pkgs.lib.licenses.zlib;
      platforms = pkgs.lib.platforms.linux;
    };
  };

  hostagesWasm = pkgs.stdenv.mkDerivation {
    pname = "hostages-wasm";
    inherit version;
    src = pkgs.lib.cleanSource ../.;
    dontConfigure = true;
    dontUseCmakeConfigure = true;
    nativeBuildInputs = [ emscripten pkgs.cmake pkgs.python3 ];
    buildPhase = ''
      runHook preBuild
      ${setupEmCache "hostages"}
      emcmake cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=$out \
        -DSDL2_ROOT=${sdl2Wasm} \
        -DPROJECT_VERSION_FULL=${version}
      cmake --build build --parallel ''${NIX_BUILD_CORES:-1}
      runHook postBuild
    '';
    installPhase = ''
      runHook preInstall
      mkdir -p $out
      cp -a build/hostages.html build/hostages.js build/hostages.wasm $out/ || true
      # Emscripten may name the html from OUTPUT_NAME
      for f in build/hostages.*; do
        [ -f "$f" ] && cp -a "$f" $out/
      done
      # Convenience alias
      if [ -f $out/hostages.html ] && [ ! -f $out/index.html ]; then
        cp $out/hostages.html $out/index.html
      fi
      runHook postInstall
    '';
    dontStrip = true;
    meta = {
      description = "Hostages as a web page (WebAssembly)";
      license = pkgs.lib.licenses.gpl3Plus;
      platforms = pkgs.lib.platforms.linux;
    };
  };

  serveApp = {
    type = "app";
    program = toString (pkgs.writeShellScript "hostages-wasm-serve" ''
      export PKG=${hostagesWasm}
      export APP_NAME=hostages
      export PATH=${pkgs.python3}/bin:${pkgs.xdg-utils}/bin:$PATH
      exec bash ${../mk/wasm/scripts/serve.sh} "$@"
    '');
    meta.description = "Serve the WebAssembly build of Hostages and open it in a browser";
  };

in {
  inherit sdl2Wasm hostagesWasm serveApp;
}
