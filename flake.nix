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
          # clang-format, for the repository's .clang-format.
          pkgs.clang-tools
          pkgs.cmake
          # Mozc's minimum. Python 3.13+ verifies certificates strictly by default
          # (VERIFY_X509_STRICT), which rejects common TLS-inspection proxy CAs.
          pkgs.python312
        ];
      };
    };
}
