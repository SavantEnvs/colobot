#!/usr/bin/env bash
#
# mayhem/build.sh — build this repo's fuzz harness(es). EDIT per repo.
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem. The base image
# (ghcr.io/savantenvs/base) already exports the build contract — use these, don't redefine:
#   CC, CXX             stock clang / clang++
#   LIB_FUZZING_ENGINE  -fsanitize=fuzzer   (link into each harness that has a LLVMFuzzer entry)
#   SANITIZER_FLAGS     -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
#                       (ASan + UBSan, both set to HALT — so Mayhem catches memory AND UB defects)
#   DEBUG_FLAGS         -g -gdwarf-3   (DWARF debug info — always on for fuzz/standalone builds,
#                       independent of the sanitizer off-switch; DWARF version must be < 4)
#   RUST_DEBUG_FLAGS    -C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3
#                       (thread through RUSTFLAGS on every cargo-fuzz build)
#   GO_DEBUG_FLAGS      -gcflags=all=-N -l
#                       (thread through go build / go-fuzz-build so the linked ELF keeps symbols)
#   SRC                 /mayhem (the repo source)
#
# Contract: build EVERYTHING here — one runnable binary per fuzz harness, AND the project's test
# suite (so mayhem/test.sh only has to RUN it, never compile). Keep it ADDITIVE (build upstream as
# upstream documents; don't edit upstream files). IMPORTANT: build the PROJECT ITSELF with
# $SANITIZER_FLAGS and $DEBUG_FLAGS (not just the harness) so the fuzzed code is instrumented
# AND carries DWARF < 4 symbols — otherwise ASan/UBSan only see the harness, not the library
# you're trying to find bugs in, and backtraces won't resolve project source lines. Build the TEST
# suite with the project's NORMAL flags (a clean, independent build) so test.sh stays an honest
# functional oracle and won't false-fail on benign UB. Leave the test binary/runner where test.sh
# expects it.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# Build knobs come from the ENVIRONMENT (overridable), with sane defaults — no if-statements,
# just parameter-expansion fallbacks. Default sanitizers are the base's ASan+UBSan (halting); override
# per build via the Dockerfile's `--build-arg SANITIZER_FLAGS="..."`.
# NB: SANITIZER_FLAGS uses `=` (no colon) on purpose — `=` only fills when the var is UNSET, so an
# explicit EMPTY value (`--build-arg SANITIZER_FLAGS=`) is honored and builds with NO sanitizers
# (useful when you want the program's natural crash / full backtrace, not an ASan report). The other
# knobs use `:=` (default on empty too). MAYHEM_JOBS sets build parallelism (falls back to nproc).
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# DEBUG_FLAGS carries DWARF debug info INDEPENDENTLY of the sanitizer off-switch (so an empty
# SANITIZER_FLAGS still yields DWARF symbols). DWARF MUST be < 4 (Mayhem triage can't read >=4); clang-19's
# plain `-g` emits DWARF-5, so `-gdwarf-3` is explicit. Apply $DEBUG_FLAGS to the fuzz/harness/standalone
# builds (NOT the test/oracle build). Rust/Go carry the same intent via their language flags below.
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${RUST_DEBUG_FLAGS:=-C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3}"
: "${GO_DEBUG_FLAGS:=-gcflags=all=-N -l}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
# COVERAGE_FLAGS: empty by default → no effect on the normal oracle build. Set it via the Dockerfile's
# `--build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping"` to instrument the TEST
# build for source-coverage measurement (how much of the project the test suite actually exercises —
# a quality signal for the oracle; complements the anti-reward-hack sabotage check). APPEND it to the
# test build's compile+link flags in step 3 (NOT the fuzz build); after `test.sh` runs, merge with
# `llvm-profdata` and report with `llvm-cov`. Empty value is honored (`=`, not `:=`).
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS RUST_DEBUG_FLAGS GO_DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# Upstream's top-level CMake configures the whole game (SDL2/GL/OpenAL/PhysFS/...). The fuzzed
# libraries (CBot, the scripting language, and Colobot-Common, the UTF-8 string utilities) are
# standalone, so mayhem/CMakeLists.txt adds just those two upstream subdirectories (their own
# CMakeLists, unchanged) plus upstream's gtest unit tests for them.
# Build dirs are /build-* at the repo root: upstream's .gitignore covers `/build*`.

# UBSan's `vptr` check is relaxed (only it; ASan and the rest of UBSan stay on and halting): every
# CBotDef{Int,Float,Boolean,String}::Compile does `static_cast<CBotDefInt*>(CompileArray(...))` on a
# CBotDefArray, so any script declaring an array (`int[] a;`, also in the restore target's built-in
# program) aborts in the compiler before reaching anything else. The pointer is only used as a
# CBotInstr*, so the downcast is benign.
SANITIZER_FLAGS="$SANITIZER_FLAGS${SANITIZER_FLAGS:+ -fno-sanitize=vptr}"

# 1) Sanitized, instrumented CBot + Colobot-Common.
FUZZ_FLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -fsanitize=fuzzer-no-link -O1"
cmake -S mayhem -B build-mayhem-fuzz -DCMAKE_BUILD_TYPE=None \
      -DCMAKE_C_FLAGS="$FUZZ_FLAGS" -DCMAKE_CXX_FLAGS="$FUZZ_FLAGS" \
      -DMAYHEM_TESTS=OFF
cmake --build build-mayhem-fuzz -j"$MAYHEM_JOBS"
LIBS="build-mayhem-fuzz/CBot/libCBot.a build-mayhem-fuzz/colobot-common/libColobot-Common.a"
INC="-ICBot/src -Icolobot-common/src"

# 2) Harnesses: libFuzzer binary + standalone reproducer each.
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o build-mayhem-fuzz/lsan_off.o
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o build-mayhem-fuzz/standalone_main.o
for h in cbot_compile_run_fuzzer cbot_restore_state_fuzzer stringutils_utf8_fuzzer; do
  $CXX -std=c++20 $SANITIZER_FLAGS $DEBUG_FLAGS -O1 $INC -c "mayhem/harnesses/$h.cpp" -o "build-mayhem-fuzz/$h.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "build-mayhem-fuzz/$h.o" $LIBS \
      build-mayhem-fuzz/lsan_off.o -o "/mayhem/$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS build-mayhem-fuzz/standalone_main.o "build-mayhem-fuzz/$h.o" $LIBS \
      build-mayhem-fuzz/lsan_off.o -o "/mayhem/$h-standalone"
done

# 2b) Colobot-Base targets (model loaders, level parser): mayhem/base/CMakeLists.txt configures upstream's whole
#     top-level project with the same sanitizer/debug flags and links the harness (libFuzzer and
#     standalone variants) against its Colobot-Base library.
cmake -S mayhem/base -B build-mayhem-base -DCMAKE_BUILD_TYPE=None \
      -DCMAKE_C_FLAGS="$FUZZ_FLAGS" -DCMAKE_CXX_FLAGS="$FUZZ_FLAGS" \
      -DCMAKE_EXE_LINKER_FLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS" \
      -DMAYHEM_FUZZ_ENGINE="$LIB_FUZZING_ENGINE" \
      -DMAYHEM_STANDALONE_OBJ="$SRC/build-mayhem-fuzz/standalone_main.o"
cmake --build build-mayhem-base -j"$MAYHEM_JOBS" --target model_input_fuzzer model_input_fuzzer-standalone \
      level_parser_fuzzer level_parser_fuzzer-standalone
for h in model_input_fuzzer level_parser_fuzzer; do
  cp "build-mayhem-base/$h" "build-mayhem-base/$h-standalone" /mayhem/
done

# 3) Upstream's CBot + stringutils gtest suite with NORMAL flags (run by mayhem/test.sh).
cmake -S mayhem -B build-mayhem-tests -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS" \
      -DMAYHEM_TESTS=ON
cmake --build build-mayhem-tests -j"$MAYHEM_JOBS" --target cbot-unit-tests
