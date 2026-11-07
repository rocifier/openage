{
  # Experimental Nix native build; Windows/Linux CPack archives are the supported releases.
  description = "Fast Villager Defense native C++ game and relay";
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-23.11";
    flake-utils.url = "github:numtide/flake-utils";
  };
  outputs = { self, nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let pkgs = import nixpkgs { inherit system; }; in
      {
        packages = rec {
          nyan = pkgs.callPackage ./nix/nyan.nix { };
          fvd-native = pkgs.callPackage ./nix/openage.nix {
            inherit (self.packages.${system}) nyan;
          };
          default = fvd-native;
        };
        apps = rec {
          fvd-native = flake-utils.lib.mkApp {
            drv = self.packages.${system}.fvd-native;
            exe = "/bin/openage-native";
          };
          default = fvd-native;
        };
      });
}
