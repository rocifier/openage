# FVD native-only package. Experimental: validated player archives are Windows/Linux CPack.
{ pkgs, lib, stdenv, nyan, ... }:
stdenv.mkDerivation {
  pname = "fvd-native";
  version = "0.6.0";
  src = ../.;

  nativeBuildInputs = with pkgs; [ cmake ninja pkg-config qt6.wrapQtAppsHook ];
  buildInputs = with pkgs; [
    nyan qt6.full qt6.qtdeclarative qt6.qtmultimedia qt6.qtshadertools
    eigen libepoxy libogg libpng
    fontconfig freetype harfbuzz opusfile libopus
    toml11
  ];

  cmakeFlags = [ "-DFVD_NATIVE_RELEASE=ON" "-DBUILD_TESTING=OFF" ];

  # Generic openage/Python install paths are not supported by FVD.
  installPhase = ''
    runHook preInstall
    cmake --install . --prefix "$out" --component FVDNative
    runHook postInstall
  '';

  meta = with lib; {
    description = "Fast Villager Defense native game and relay";
    license = licenses.gpl3Plus;
    platforms = platforms.unix;
    mainProgram = "openage-native";
  };
}
