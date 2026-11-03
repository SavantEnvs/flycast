#!/usr/bin/env bash
#
# mayhem/build.sh — build flycast's disc-image / ini / ISO9660 fuzz harnesses and the unit-test runner.
#
# Flycast's full build needs SDL/GL/Vulkan and ~20 submodules; the code worth fuzzing (imgread/*, the
# ini parser, libchdr) is self-contained, so this compiles just those sources plus a small mock of
# the emulator globals (mayhem/flycast_stubs.cpp). Two independent builds of the same sources:
#   build/fuzz   — ASan+UBSan (halting) + DWARF 3, linked into the harnesses
#   build/tests  — the project's normal flags, linked with upstream's own gtest sources
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

: "${SRC:=/mayhem}"
cd "$SRC"

# The two submodules this build needs (libchdr and googletest). They are fetched at the commit the
# gitlink pins; once present (image build) the offline re-run skips this.
for sm in core/deps/libchdr core/deps/googletest; do
  [ -f "$sm/CMakeLists.txt" ] || git submodule update --init -- "$sm"
done

INC=(-I"$SRC/mayhem/stubs" -I"$SRC/core" -I"$SRC/core/deps" -I"$SRC/core/deps/nowide/include"
     -I"$SRC/core/deps/libchdr/include" -I"$SRC/tests/inc")
CORE_SOURCES=(
  core/imgread/cdi.cpp core/imgread/chd.cpp core/imgread/common.cpp core/imgread/cue.cpp
  core/imgread/gdi.cpp core/imgread/isofs.cpp core/imgread/ImgReader.cpp
  core/stdclass.cpp core/cfg/ini.cpp core/oslib/storage.cpp
  core/deps/md5/md5.cpp core/deps/chdpsr/cdipsr.cpp
  mayhem/flycast_stubs.cpp
)

# build_variant <dir> <c/c++ flags> [extra libchdr-only C flags]: libchdr (+ its bundled zlib/zstd/lzma)
# and the core objects.
build_variant() {
  local dir="$1" flags="$2" chdflags="${3:-}"
  mkdir -p "$dir/obj"
  cmake -S core/deps/libchdr -B "$dir/chdr" -G Ninja -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_BUILD_TYPE=None -DCMAKE_C_COMPILER="$CC" \
        -DCMAKE_C_FLAGS="$flags $chdflags" >/dev/null
  cmake --build "$dir/chdr" --target chdr-static -j"$MAYHEM_JOBS" >/dev/null
  printf '%s\n' "${CORE_SOURCES[@]}" | xargs -P "$MAYHEM_JOBS" -I{} \
      sh -c 'o="$1/obj/$(echo "$2" | tr / _).o"; shift; shift; exec "$@" -o "$o" "$2"' _ "$dir" {} \
      "$CXX" -std=c++17 $flags "${INC[@]}" -c {} 2>&1
}

# fuzzer-no-link adds libFuzzer's edge instrumentation to every fuzz-variant object (core + libchdr).
SAN="$SANITIZER_FLAGS -fsanitize=fuzzer-no-link $DEBUG_FLAGS -O1"
# libchdr's vendored LZMA code (mismatched allocator callback type, unaligned UInt32 stores) and its
# bitstream reader (shift into the sign bit, and a shift count of 0/32 or a negative one from an untrusted
# bit count in bitstream_peek/bitstream_remove) trip UBSan on valid and malformed CHDs alike though benign
# on x86 (the count is masked), so only those four checks are relaxed, and only for libchdr's C sources.
# Bad bit counts that go on to corrupt memory still surface as ASan reports.
CHD_RELAX="-fno-sanitize=function,shift-base,shift-exponent,alignment"
GTEST_SRC=core/deps/googletest/googletest
TEST_SOURCES=(tests/src/IniFileTest.cpp tests/src/imgread/ChdTest.cpp tests/src/imgread/CueTest.cpp
              tests/src/imgread/GdiTest.cpp tests/src/util/Md5SumTest.cpp mayhem/ZipArchiveTest.cpp)

# build_zip <dir> <c/c++ flags>: the in-tree libzip (core/deps/libzip) and flycast's ZipArchive wrapper over it,
# for fuzz_zip and the ZipArchive gtest. Only the deflate/store paths are built (no crypto/bzip2/lzma/zstd:
# none are needed to load a zipped ROM here); zlib comes from the base's zlib1g-dev.
build_zip() {
  local dir="$1" flags="$2"
  cmake -S core/deps/libzip -B "$dir/zip" -G Ninja -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=None \
        -DCMAKE_C_COMPILER="$CC" -DCMAKE_C_FLAGS="$flags" \
        -DENABLE_COMMONCRYPTO=OFF -DENABLE_GNUTLS=OFF -DENABLE_MBEDTLS=OFF -DENABLE_OPENSSL=OFF \
        -DENABLE_WINDOWS_CRYPTO=OFF -DENABLE_BZIP2=OFF -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF \
        -DBUILD_TOOLS=OFF -DBUILD_REGRESS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF -DLIBZIP_DO_INSTALL=OFF >/dev/null
  cmake --build "$dir/zip" --target zip -j"$MAYHEM_JOBS" >/dev/null
  "$CXX" -std=c++17 $flags "${INC[@]}" -I"$SRC/core/deps/libzip/lib" -I"$SRC/$dir/zip" \
      -c core/archive/ZipArchive.cpp -o "$dir/ZipArchive.o"
}

# 1) Unit tests with the project's normal flags (clean, independent of the sanitized build).
TFLAGS="-O1 $COVERAGE_FLAGS"
build_variant build/tests "$TFLAGS"
build_zip build/tests "$TFLAGS"
mkdir -p build/tests/gtest
GT_INC=(-I"$GTEST_SRC" -I"$GTEST_SRC/include")
"$CXX" -std=c++17 $TFLAGS "${GT_INC[@]}" -c "$GTEST_SRC/src/gtest-all.cc" -o build/tests/gtest/gtest-all.o
"$CXX" -std=c++17 $TFLAGS "${GT_INC[@]}" -c "$GTEST_SRC/src/gtest_main.cc" -o build/tests/gtest/gtest_main.o
TOBJS=()
for t in "${TEST_SOURCES[@]}"; do
  o="build/tests/gtest/$(basename "$t").o"
  "$CXX" -std=c++17 $TFLAGS -DFLYCAST_TEST_FILES="\"$SRC/tests/files\"" -DMAYHEM_SEEDS_DIR="\"$SRC/mayhem/fuzz_zip/testsuite\"" \
    -I"$SRC/core/deps/libzip/lib" -I"$SRC/build/tests/zip" "${INC[@]}" "${GT_INC[@]}" -c "$t" -o "$o" &
  TOBJS+=("$o")
done
wait
CHDLIBS() { echo "$1/chdr/libchdr-static.a" "$1"/chdr/deps/lzma-*/liblzma.a "$1"/chdr/deps/zlib-*/libz.a \
            "$1"/chdr/deps/zstd-*/build/cmake/lib/libzstd.a; }
"$CXX" $TFLAGS "${TOBJS[@]}" build/tests/obj/*.o build/tests/gtest/gtest-all.o build/tests/gtest/gtest_main.o \
    build/tests/ZipArchive.o build/tests/zip/lib/libzip.a -lz $(CHDLIBS build/tests) -lpthread -o build/tests/flycast_tests

# 2) Sanitized core + harnesses (fuzzer and -standalone variants).
build_variant build/fuzz "$SAN" "$CHD_RELAX"

build_zip build/fuzz "$SAN"
ZIPINC=(-I"$SRC/core/deps/libzip/lib" -I"$SRC/build/fuzz/zip")

"$CC" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o build/fuzz/standalone_main.o
"$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o build/fuzz/lsan_off.o
# The standalone reproducers link no libFuzzer runtime; with sanitizers on, ASan's runtime already provides
# the coverage callbacks, and these weak no-ops cover the sanitizer-off build.
"$CC" $DEBUG_FLAGS -c mayhem/sancov_stub.c -o build/fuzz/sancov_stub.o

HARNESS_LIBS=()
# harness <name> <source> [extra compile flags...]
harness() {
  local name="$1" srcf="$2"; shift 2
  "$CXX" -std=c++17 $SAN "$@" "${INC[@]}" "${ZIPINC[@]}" -c "$srcf" -o "build/fuzz/h_$name.o"
  local common=(build/fuzz/obj/*.o build/fuzz/lsan_off.o $(CHDLIBS build/fuzz) -lpthread "${HARNESS_LIBS[@]}")
  "$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "build/fuzz/h_$name.o" "${common[@]}" -o "$SRC/$name"
  "$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link build/fuzz/standalone_main.o build/fuzz/sancov_stub.o "build/fuzz/h_$name.o" "${common[@]}" -o "$SRC/$name-standalone"
}
harness fuzz_gdi   mayhem/fuzz_disc.cpp '-DDISC_EXT="gdi"' -DFUZZ_TRACK_FILES
harness fuzz_cue   mayhem/fuzz_disc.cpp '-DDISC_EXT="cue"' -DFUZZ_TRACK_FILES
harness fuzz_cdi   mayhem/fuzz_disc.cpp '-DDISC_EXT="cdi"'
harness fuzz_chd   mayhem/fuzz_disc.cpp '-DDISC_EXT="chd"'
harness fuzz_isofs mayhem/fuzz_isofs.cpp
harness fuzz_ini   mayhem/fuzz_ini.cpp
HARNESS_LIBS=(build/fuzz/ZipArchive.o build/fuzz/zip/lib/libzip.a -lz)
harness fuzz_zip   mayhem/fuzz_zip.cpp
