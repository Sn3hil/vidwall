{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  buildInputs = with pkgs; [

    meson
    ninja
    pkg-config
    gtk4
    gtk4-layer-shell
    mpv
    libepoxy 
    gdb
    clang-tools
    
  ];
  
  shellHook = ''
    echo "vidwall development environment"
  '';
}
