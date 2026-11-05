{
  description = "Goose compiler development environment with 32-bit ARM Cortex-M3 (QEMU/Zephyr) and WebAssembly toolchains";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixpkgs-26.05-darwin";
  };

  outputs = { self, nixpkgs }:
    let
      supportedSystems = [ "aarch64-darwin" "x86_64-darwin" "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    in
    {
      devShells = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
          wasiClang = pkgs.pkgsCross.wasi32.buildPackages.clang;
          wasiBins = pkgs.runCommand "wasi-clang-bins" {} ''
            mkdir -p $out/bin
            ln -s ${wasiClang}/bin/wasm32-unknown-wasi-* $out/bin/
          '';
          zephyrSrc = pkgs.fetchFromGitHub {
            owner = "zephyrproject-rtos";
            repo = "zephyr";
            rev = "v3.7.0";
            hash = "sha256-rmOHH0uRU27U2T4w4+FEMcAcuiZ7W7p4vOwtSwiAFNY=";
          };
          cmsisSrc = pkgs.fetchFromGitHub {
            owner = "zephyrproject-rtos";
            repo = "cmsis";
            rev = "4b96cbb174678dcd3ca86e11e1f24bc5f8726da0";
            hash = "sha256-vzCbE69wCMLIjrSz1m8yspgVCto4JSm4Qg5zY/Ozg+k=";
          };
          zephyrWorkspace = pkgs.runCommand "zephyr-workspace" {} ''
            mkdir -p $out/.west $out/modules/hal
            cat > $out/.west/config <<EOF
            [manifest]
            path = zephyr
            file = west.yml

            [zephyr]
            base = zephyr
            EOF
            mkdir -p $out/zephyr
            for entry in ${zephyrSrc}/*; do
              name=$(basename "$entry")
              if [ "$name" != "scripts" ] && [ "$name" != "arch" ]; then
                ln -s "$entry" "$out/zephyr/$name"
              fi
            done
            cp -r ${zephyrSrc}/scripts $out/zephyr/scripts
            cp -rs ${zephyrSrc}/arch $out/zephyr/arch
            chmod -R u+w $out/zephyr/arch/arm/core/cortex_m
            rm $out/zephyr/arch/arm/core/cortex_m/thread.c
            sed 's/: "r0", "r1", "r2", "r3", "r4", "ip", "lr");/: "r0", "r1", "r2", "r3", "r4", "ip", "lr", "memory");/' \
              ${zephyrSrc}/arch/arm/core/cortex_m/thread.c > $out/zephyr/arch/arm/core/cortex_m/thread.c
            ln -s ${cmsisSrc} $out/modules/hal/cmsis
          '';
          pythonEnv = pkgs.python3.withPackages (ps: with ps; [
            west
            pyelftools
            pyyaml
            packaging
            pykwalify
            canopen
            intelhex
            psutil
          ]);
        in
        {
          default = pkgs.mkShell {
            packages = [
              # Host compiler build tools
              pkgs.cmake
              pkgs.ninja
              pythonEnv

              # 32-bit ARM Cortex-M3 & Zephyr RTOS toolchain + QEMU emulator
              pkgs.gcc-arm-embedded
              pkgs.qemu
              pkgs.dtc
              pkgs.gperf

              # 32-bit WebAssembly (WASI + Emscripten) compilers & runner
              wasiBins
              pkgs.lld
              pkgs.wasmtime
              pkgs.emscripten
            ];

            shellHook = ''
              export PATH="${pkgs.stdenv.cc}/bin:$PATH"
              export EM_CACHE="''${TMPDIR:-/tmp}/goose-emcache"
              export ZEPHYR_BASE="${zephyrWorkspace}/zephyr"
              export ZEPHYR_MODULES="${zephyrWorkspace}/modules/hal/cmsis"
              export ZEPHYR_TOOLCHAIN_VARIANT="gnuarmemb"
              export GNUARMEMB_TOOLCHAIN_PATH="${pkgs.gcc-arm-embedded}"
            '';
          };
        }
      );
    };
}
