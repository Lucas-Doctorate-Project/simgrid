{ kapack ? import
    (fetchTarball "https://github.com/oar-team/nur-kapack/archive/master.tar.gz")
  {}
, doUnitTests ? true
, doCoverage ? true
, werror ? false
, debug ? true
, useClang ? builtins.elem builtins.currentSystem [ "x86_64-darwin" "aarch64-darwin" ]
}:

let
  pkgs = kapack.pkgs;

  boolFlag = value: if value then "ON" else "OFF";

  s4uTestsRegex = "^(s4u-|tesh-s4u-|monkey-s4u-)";
  darwinDisabledS4uTestsRegex = "^s4u-exec-env-footprint-thread$";

  custom-stdenv-base = if useClang then pkgs.clangStdenv else pkgs.gccStdenv;
  custom-stdenv = if debug then (pkgs.stdenvAdapters.keepDebugInfo custom-stdenv-base) else custom-stdenv-base;

  jobs = rec {
    inherit pkgs;
    inherit kapack;

    # SimGrid library and tools.
    simgrid = (kapack.simgrid-light.override { stdenv = custom-stdenv; }).overrideAttrs (attr: rec {
      src = pkgs.lib.sourceByRegex ./. [
        "^CMakeLists\\.txt$"
        "^FindSimGrid\\.cmake$"
        "^MANIFEST\\.in$"
        "^MANIFEST\\.in\\.in$"
        "^setup\\.py$"
        "^COPYING$"
        "^ChangeLog$"
        "^LICENSE-LGPL-2\\.1$"
        "^NEWS$"
        "^README\\.md$"
        "^include(/.*)?$"
        "^src(/.*)?$"
        "^tools(/.*)?$"
        "^examples(/.*)?$"
        "^teshsuite(/.*)?$"
        "^docs(/manpages(/.*)?)?$"
      ];

      cmakeBuildType = if debug then "Debug" else "Release";
      cmakeFlags = (attr.cmakeFlags or []) ++ [
        "-DCMAKE_BUILD_TYPE=${cmakeBuildType}"
        "-Denable_debug=${boolFlag debug}"
        "-Denable_compile_optimizations=${boolFlag (!debug)}"
        "-Denable_compile_warnings=${boolFlag werror}"
        "-Denable_coverage=${boolFlag doCoverage}"
        "-DBUILD_TESTING=${boolFlag doUnitTests}"
      ];

      doCheck = doUnitTests;
      checkPhase = ''
        runHook preCheck
        cmake --build . --target tests
        ctest --output-on-failure -R '${s4uTestsRegex}' ${pkgs.lib.optionalString pkgs.stdenv.isDarwin "-E '${darwinDisabledS4uTestsRegex}'"}
        runHook postCheck
      '';

      postInstall = (attr.postInstall or "") + pkgs.lib.optionalString doCoverage ''
        copyCoverageFiles() {
          local dest="$1"
          local pattern="$2"

          mkdir -p "$dest"
          while IFS= read -r -d "" file; do
            local target="$dest/''${file#./}"
            mkdir -p "$(dirname "$target")"
            cp "$file" "$target"
          done < <(find . -name "$pattern" -type f -print0)
        }

        copyCoverageFiles "$out/gcno" "*.gcno"
      '' + pkgs.lib.optionalString (doCoverage && doUnitTests) ''
        copyCoverageFiles "$out/gcda" "*.gcda"
      '';
    });
  };
in
  jobs
