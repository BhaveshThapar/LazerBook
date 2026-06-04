#!/usr/bin/env bash
# Reproducible Linux/x86 bench run: pin a core, fix the governor, kill turbo,
# build -march=native, and dump results to a timestamped directory.
set -euo pipefail

if [[ "$(uname -s)" != "Linux" ]]; then
    echo "error: bench-linux.sh is Linux-only" >&2
    exit 1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PIN_CORE="${PIN_CORE:-3}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT="bench-results/$STAMP"
mkdir -p "$OUT"

# Pin governor to performance and disable turbo (best-effort; needs root).
if [[ -w /sys/devices/system/cpu/cpu${PIN_CORE}/cpufreq/scaling_governor ]]; then
    echo performance > "/sys/devices/system/cpu/cpu${PIN_CORE}/cpufreq/scaling_governor" || true
fi
if [[ -w /sys/devices/system/cpu/intel_pstate/no_turbo ]]; then
    echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo || true
fi

CXXFLAGS="-march=native -mtune=native" \
    cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DLAZERBOOK_BUILD_BENCH=ON
cmake --build build-bench -j

run() { echo "== $1 =="; taskset -c "$PIN_CORE" "$@"; }

{
    run ./build-bench/bench/bench_matcher
    run ./build-bench/bench/bench_power
    run ./build-bench/bench/replay_validate 5000000
} | tee "$OUT/results.txt"

echo "results written to $OUT/results.txt"
