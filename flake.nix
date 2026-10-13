{
  description = "Hostages (Infogrames) – C++/SDL2 port of the Amiga version";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
        hostages = pkgs.stdenv.mkDerivation {
          pname = "hostages";
          version = "0.1.0";
          src = self;

          nativeBuildInputs = with pkgs; [
            cmake
            pkg-config
          ];

          buildInputs = with pkgs; [
            SDL2
	    sdl3
          ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
          ];

          meta = with pkgs.lib; {
            mainProgram = "hostages";
            description = "Port of the Amiga version of Hostages (Infogrames, 1988)";
            license = licenses.gpl3Plus;
            platforms = platforms.linux;
          };
        };
      in
      {
        packages.default = hostages;
        packages.hostages = hostages;

        apps.default = { type = "app"; program = "${hostages}/bin/hostages"; };

        devShells.default = pkgs.mkShell {
          packages = with pkgs; [
            cmake
            pkg-config
            SDL2
	    sdl3
            # useful for RE
            python3
            xxd
            file
            # m68k objdump for tools/disasm.py
            pkgsCross.m68k.buildPackages.binutils
          ];
          shellHook = ''
            echo "Hostages dev shell"
            echo "  cmake -B build && cmake --build build"
            echo "  ./build/hostages"
          '';
        };
      });
}
