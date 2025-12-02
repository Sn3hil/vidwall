{
  description = "Hyprland Video Wallpaper";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "vidwall";
          version = "0.1.0";

          src = ./.;

          nativeBuildInputs = with pkgs; [
            meson
            ninja
            pkg-config
          ];

          buildInputs = with pkgs; [
            gtk4
            gtk4-layer-shell
            mpv
            libepoxy
          ];

          meta = with pkgs.lib; {
            description = "Video wallpaper for Hyprland with auto-pause";
            license = licenses.mit;
            platforms = platforms.linux;
            mainProgram = "vidwall";
          };
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.default ];
        };
      }
    );
}
