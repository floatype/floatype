{
  description = "floatype development shell";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs =
    { nixpkgs, ... }:
    let
      pkgs = nixpkgs.legacyPackages.aarch64-darwin;
    in
    {
      # mkShellNoCC keeps Xcode's clang and SDK in charge; Mozc's Bazel build requires Xcode.
      devShells.aarch64-darwin.default = pkgs.mkShellNoCC {
        packages = [
          pkgs.bazelisk
          pkgs.cmake
          pkgs.python3
        ];
      };
    };
}
