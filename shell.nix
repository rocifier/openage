# Native FVD development shell. No Python/Cython converter packages.
{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  nativeBuildInputs = with pkgs; [
    clang cmake ninja gnumake gcc
    eigen libepoxy libogg libpng
    fontconfig freetype harfbuzz opusfile libopus
    toml11
    qt6.full qt6.qtdeclarative qt6.qtmultimedia
  ];
}
