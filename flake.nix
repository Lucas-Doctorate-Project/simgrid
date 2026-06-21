{
  description = "SimGrid - Lucas Doctorate Project fork";

  inputs = {
    flake-parts.url = "github:hercules-ci/flake-parts";
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    kapack = {
      url = "github:oar-team/nur-kapack/master";
      flake = false;
    };
  };

  outputs = inputs@{ flake-parts, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [ "x86_64-linux" "aarch64-darwin" ];

      perSystem = { inputs', pkgs, system, ... }:
        let
          kapack = import inputs.kapack { inherit pkgs; };
          baseStdenv =
            if builtins.elem system [ "x86_64-darwin" "aarch64-darwin" ]
            then pkgs.clangStdenv
            else pkgs.gccStdenv;
          stdenvFor = debug:
            if debug then pkgs.stdenvAdapters.keepDebugInfo baseStdenv else baseStdenv;
          makeSimgrid = { debug, doCoverage ? false, doUnitTests ? false, werror ? false }:
            pkgs.callPackage ./nix/simgrid.nix {
              simgridPackage = kapack.simgrid-light;
              stdenv = stdenvFor debug;
              inherit debug doCoverage doUnitTests werror;
            };

          simgrid = makeSimgrid {
            debug = false;
            doCoverage = false;
            doUnitTests = false;
          };
          simgrid-debug = makeSimgrid {
            debug = true;
            doCoverage = false;
            doUnitTests = true;
          };
          simgrid-coverage = makeSimgrid {
            debug = true;
            doCoverage = true;
            doUnitTests = true;
          };
        in {
          packages.default = simgrid;
          packages.simgrid = simgrid;
          packages.simgrid-debug = simgrid-debug;
          packages.simgrid-coverage = simgrid-coverage;

          devShells.default = pkgs.mkShell {
            inputsFrom = [ simgrid-debug ];
            packages = [
              pkgs.cmake
              pkgs.ninja
              pkgs.pkg-config
            ];
          };
        };
    };
}
