# SPDX-License-Identifier: BSD-2-Clause
{
  description = "Tensor-seL4: seL4 Microkit port to Pixel 9 Pro (caiman / zumapro)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/8c50a710ddca43d7a530fb805ad55bde8d0141c5";
    rust-overlay = {
      url = "github:oxalica/rust-overlay/49a67e6894d4cb782842ee6faa466aa90c92812d";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = { self, nixpkgs, rust-overlay }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f (import nixpkgs {
        inherit system;
        overlays = [ (import rust-overlay) ];
      }));
    in
    {
      devShells = forAllSystems (pkgs:
        let
          aarch64Embedded = pkgs.pkgsCross.aarch64-embedded.stdenv.cc;

          python = pkgs.python312.withPackages (ps: [
            ps.ply
            ps.jinja2
            ps.pyaml
            ps.lxml
            ps.pyfdt
            ps.setuptools
            ps.jsonschema
          ]);

          # TODO(will): keep in sync with deps/microkit/tool/microkit/Cargo.toml rust-version
          rust = pkgs.rust-bin.stable."1.94.0".default.override {
            extensions = [ "rust-src" ];
            targets = [
              pkgs.pkgsStatic.stdenv.hostPlatform.rust.rustcTarget
              "aarch64-unknown-none"
            ];
          };
        in
        {
          default = pkgs.mkShell {
            name = "tensor-sel4";

            nativeBuildInputs = [
              aarch64Embedded
              aarch64Embedded.bintools
              rust
              python
              pkgs.gnumake
              pkgs.cmake
              pkgs.ninja
              pkgs.dtc
              pkgs.libxml2
              pkgs.git
              pkgs.gitRepo
              pkgs.gnutar
              pkgs.qemu
              pkgs.android-tools
            ];

            LIBCLANG_PATH = "${pkgs.llvmPackages_18.libclang.lib}/lib";
          };
        });
    };
}
