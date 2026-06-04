#!/usr/bin/env bash
# Line/branch coverage for the test suite (clang/llvm only). Targets >=85% line,
# >=75% branch (v0.3.0 measures 90.2 / 81.7 on Linux/clang-18).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

BUILD=build-coverage

# Prefer plain llvm tools; fall back to the Xcode toolchain via xcrun (macOS).
if command -v llvm-profdata >/dev/null 2>&1; then
    PROFDATA=llvm-profdata
    COV=llvm-cov
elif command -v xcrun >/dev/null 2>&1; then
    PROFDATA="xcrun llvm-profdata"
    COV="xcrun llvm-cov"
else
    echo "error: need llvm-profdata / llvm-cov (or xcrun)" >&2
    exit 1
fi

cmake -S . -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DLAZERBOOK_COVERAGE=ON \
    -DCMAKE_CXX_COMPILER="${CXX:-clang++}"
cmake --build "$BUILD" -j

BIN="$BUILD/tests/test_lazerbook"
LLVM_PROFILE_FILE="$BUILD/lazerbook.profraw" "$BIN"

$PROFDATA merge -sparse "$BUILD/lazerbook.profraw" -o "$BUILD/lazerbook.profdata"
$COV report "$BIN" \
    -instr-profile="$BUILD/lazerbook.profdata" \
    -ignore-filename-regex='_deps|tests/test_'
