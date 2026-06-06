# Benchmark methodology

How each measurement is taken, and how to reproduce it.

## `bench_matcher`

Three scenarios, 100k iterations each, with `now_ticks()` bracketing the single
matcher call and `ticks_to_ns()` converting:

- **limit-insert-no-match** — insert a resting limit that never crosses (the
  canonical hot path).
- **limit-insert-1-fill** — each insert crosses exactly one resting order.
- **cancel-resting** — cancel a previously rested order.

Reports `{p50, p90, p99, p99.9, p99.99, max, mean}` via `Percentiles::report()`.

```sh
cmake -S . -B build -DLAZERBOOK_BUILD_BENCH=ON && cmake --build build -j
./build/bench/bench_matcher
```

## `bench_power`

Runs the insert+match scenario for ~5 s wrapped in `EnergyMeasure::start()/stop()`
and prints throughput (Mops/s) and joules per million matches when RAPL is
available (Linux/x86 only).

## `replay_validate`

Generates `N` synth events (argv[1], default 5,000,000), parses each with the real
ITCH decoder, applies them to a `Book` via the reconstructor, then asserts the
reconstructed aggregates (per-side shares, best bid/ask, live order count) equal
the synth ground truth. Prints throughput.

```sh
./build/bench/replay_validate 5000000
```

## `fba_sweep`

Sweeps `batch_ticks ∈ {0(CDA), 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000}`,
averages over 3 seeds, and emits `mode,batch_ticks,sniper_pnl_mean,mm_pnl_mean,
fills_mean` to stdout. The load-bearing qualitative result: Sniper P&L is positive
under CDA and sign-flips negative as the batch window grows. See
[fba-paper-reproduction.md](fba-paper-reproduction.md).

## `diff_validate`

Built only with `LAZERBOOK_WITH_LIQUIBOOK=ON`. Drives 100k random commands through
both `LazerbookDriver` and `LiquibookDriver` and compares the canonical fill
streams. On divergence it dumps the divergent index and the ±100 surrounding fills
from each engine — a known random-stress divergence at ~50k commands on some seeds
is expected and reported, not hidden.

## Platform caveats

- **Linux / x86 (TSC).** `__rdtscp` resolves to single-digit-nanosecond ticks.
  Pin a core, set the governor to `performance`, and disable turbo with
  `tools/bench-linux.sh` for stable tails. limit-insert-no-match p99 sits under
  1 µs.
- **Apple Silicon (`steady_clock`).** The monotonic clock floor is ~42 ns/tick,
  so any p50/p90 below ~80 ns simply reports the clock floor, not the operation.
  Trust the tail (p99+) and the sustained throughput from `replay_validate`
  instead. The measured limit-insert-no-match here is p50 42 ns / p99 125 ns;
  cancel is p50 41 ns / p99 84 ns; sustained replay ~4.6 M events/s on one core.
