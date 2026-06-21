{ lib
, stdenv
, simgridPackage
, debug ? false
, doCoverage ? false
, doUnitTests ? false
, werror ? false
}:

let
  boolFlag = value: if value then "ON" else "OFF";
  s4uTestsRegex = "^(s4u-|tesh-s4u-|monkey-s4u-)";
  darwinDisabledS4uTestsRegex = "^s4u-exec-env-footprint-thread$";
  ignoredCmakeFlags = [
    "-DCMAKE_BUILD_TYPE="
    "-Denable_debug="
    "-Denable_compile_optimizations="
    "-Denable_compile_warnings="
    "-Denable_coverage="
    "-DBUILD_TESTING="
  ];
  keepCmakeFlag = flag:
    !(lib.any (prefix: lib.hasPrefix prefix flag) ignoredCmakeFlags);
in
(simgridPackage.override { inherit stdenv; }).overrideAttrs (old: rec {
  src = lib.sourceByRegex ../. [
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
  cmakeFlags =
    let
      baseCmakeFlags = old.cmakeFlags or [];
    in
    lib.filter keepCmakeFlag baseCmakeFlags ++ [
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
    ctest --output-on-failure -R '${s4uTestsRegex}' ${lib.optionalString stdenv.isDarwin "-E '${darwinDisabledS4uTestsRegex}'"}
    runHook postCheck
  '';

  postInstall = (old.postInstall or "") + lib.optionalString doCoverage ''
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
  '' + lib.optionalString (doCoverage && doUnitTests) ''
    copyCoverageFiles "$out/gcda" "*.gcda"
  '';

  passthru =
    let
      debugSrcDirs = [ "${src}/src" ];
    in
    (old.passthru or {}) // {
      hasDebugSymbols = debug;
      hasCoverage = doCoverage;
      hasTestBinaries = doUnitTests;
      GCOV_PREFIX_STRIP = "5";
      DEBUG_SRC_DIRS = debugSrcDirs;
      GDB_DIR_ARGS = map (path: "--directory=" + path) debugSrcDirs;
    };
})
