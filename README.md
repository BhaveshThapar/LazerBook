# lazerbook

> A sub-microsecond limit-order-book matching engine in modern C++ —
> with both **Continuous Double Auction** and **Frequent Batch Auction**
> matching modes, byte-exact differential testing against a reference
> engine, and a libFuzzer harness with 385M zero-crash executions.

![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-≥3.24-064F8C?logo=cmake&logoColor=white)
![CI](https://img.shields.io/badge/CI-linux%20%2B%20macOS%20%E2%88%AA%20gcc%20%2B%20clang-2ea44f)
![Coverage](https://img.shields.io/badge/coverage-90.2%25-2ea44f)
![Fuzz](https://img.shields.io/badge/fuzz-385M%20execs%20%C2%B7%200%20crashes-2ea44f)
![License](https://img.shields.io/badge/license-MIT-blue)

**[Architecture](docs/architecture.md)** ·
**[Design notes](DESIGN.md)** ·
**[Changelog](CHANGELOG.md)**

---

## Headline numbers

Apple M-series, single core, `steady_clock`, v0.2.0 — Linux/x86 run with
TSC sharpens these by ~10× and lands with v1.0.

| Scenario                         |  p50 |  p99 | p99.9 |  max |
|----------------------------------|-----:|-----:|------:|-----:|
| limit insert (no match)          | 42ns | 84ns | 2.0µs | 382µs |
| limit insert + match (1 fill)    | 42ns | 42ns |  125ns | 150µs |
| cancel resting                   | 42ns | 208ns | 375ns |  77µs |
| **sustained throughput**         | **29.4 M ops/sec** (insert+match pairs) |||

> p50 / p90 are at the Apple `steady_clock` floor (~42 ns / tick); the
> point isn't that 42 ns is the true latency — it's that the operation
> completes in less than one clock tick. Methodology in
> [`docs/methodology.md`](docs/methodology.md).

## Why this is unusual

Three things together — most public matching engines have one or two:

1. **Sub-µs hot path** — three pinned threads, lock-free SPSC rings,
   intrusive book layout, pre-allocated `OrderPool`, no `new` / no
   syscall on insert / cancel / match.
2. **A correctness moat** — 79 doctest unit tests, **90.2% line /
   81.7% branch coverage**, **byte-exact differential parity vs
   `liquibook`** (4 cases byte-equal, 1 known divergence under triage),
   **5M-event synthetic ITCH replay byte-exact**, **libFuzzer 385M
   executions under ASan + UBSan with 0 crashes**.
3. **Real research reproduction** — implements the Frequent Batch
   Auction proposal from Budish-Cramton-Shim 2015 as a peer of CDA on
   the same engine core, and reproduces the headline qualitative claim:
   the Sniper's P&L flips from **+12.8M (CDA) → −112M (FBA-100)** as
   the batch interval grows. See
   [`docs/fba-paper-reproduction.md`](docs/fba-paper-reproduction.md).

## What's a "matching engine," in plain English

A stock exchange runs a small piece of software that takes everyone's
"buy" and "sell" requests and figures out who trades with whom, at what
price, in what order. That's a matching engine. lazerbook is a
from-scratch implementation, fast enough that the typical decision
happens in roughly the time light travels 40 feet.

It also has a "slow on purpose" mode (FBA) — an academic proposal for
how exchanges *should* work to neutralize the high-frequency-trading
arms race. Because both modes share the same engine core, the same
codebase can argue both sides of the debate.

## Architecture

Three pinned threads, no locks on the hot path. CDA and FBA share the
book and the threading layout — only the fill-generation policy differs.

```
  Net thread  ──SPSC──▶  Matcher thread  ──SPSC──▶  Egress thread
                              │
                              ├─ CDA (default)
                              └─ FBA (--mode=fba --batch-ms=100)
```

Per-component deep dive: [`docs/architecture.md`](docs/architecture.md).
Design rationale: [`DESIGN.md`](DESIGN.md).

## What's in the box

```
include/lazerbook/
  types.hpp             Price4, OrderId, Side, Symbol, big-endian decode
  itch.hpp              ITCH 5.0 parser — 22 message types per spec
  itch_synth.hpp        Coherent ITCH 5.0 byte-stream generator
  order.hpp             40-byte POD with intrusive prev/next
  order_pool.hpp        Pre-allocated pool with O(1) freelist
  book.hpp              Per-side price-indexed array of intrusive lists
  events.hpp            Fill / OrderAccepted / OrderCancelled / OrderRejected
  matcher.hpp           CDA matcher: Limit / Market / IOC / FOK + cancel + modify
  fba.hpp               Frequent Batch Auction (uniform-price discrete)
  reconstruct.hpp       Apply ITCH events to a Book (replay → L2/L3)
  sim.hpp               Tiny agent simulator (Sniper, MM, ZI)
  spsc_ring.hpp         Vyukov-style cacheline-padded SPSC
  ouch.hpp              OUCH 4.2 message types (structural only)
  bench/clock.hpp       rdtsc on x86-64, std::chrono fallback
  bench/percentile.hpp  Sort-on-report percentile tracker
  bench/rapl.hpp        Linux RAPL energy reader
  diff/canonical.hpp    Canonical fill schema for differential testing
  diff/harness.hpp      Two-engine differential runner
  diff/lazerbook_driver.hpp   Driver wrapping our own Matcher
  diff/liquibook_driver.hpp   Driver wrapping liquibook (gated)

bench/
  bench_matcher.cpp     Latency benchmark: insert / insert+match / cancel
  bench_power.cpp       Throughput + RAPL energy
  fba_compare.cpp       Same agent population through CDA and FBA
  fba_sweep.cpp         FBA paper reproduction sweep
  replay_validate.cpp   Synth ITCH → parse → reconstruct → ground-truth
  diff_validate.cpp     Random-stress differential vs liquibook
  fuzz_itch.cpp         libFuzzer harness for the parser

src/                    Implementation files
tests/                  79 doctest unit tests
docs/                   Architecture, methodology, diff schema, fuzz log
tools/                  Coverage, Linux bench, fuzz seed corpus scripts
```

## Quickstart

```bash
git clone --recursive https://github.com/<you>/lazerbook.git
cd lazerbook
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run the matcher latency bench:

```bash
cmake -S . -B build -DLAZERBOOK_BUILD_BENCH=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/bench/bench_matcher
./build/bench/bench_power      # adds RAPL energy on Linux
./build/bench/fba_compare      # CDA vs FBA on identical agent pop
./build/bench/fba_sweep        # paper reproduction sweep
./build/bench/replay_validate  # synth ITCH → parse → reconstruct
```

Run the parser fuzzer (clang only):

```bash
cmake -S . -B build-fuzz -DLAZERBOOK_BUILD_BENCH=ON -DLAZERBOOK_BUILD_FUZZ=ON \
      -DCMAKE_CXX_COMPILER=clang++
cmake --build build-fuzz --target fuzz_itch
./build-fuzz/bench/fuzz_itch -max_total_time=300
```

Run the differential test against `liquibook` (vendored as a submodule):

```bash
cmake -S . -B build -DLAZERBOOK_BUILD_BENCH=ON -DLAZERBOOK_WITH_LIQUIBOOK=ON \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build --target diff_validate -j
./build/bench/diff_validate
```

## Validation moat

| Tool | Run at v0.3.0 | What it catches |
|------|---------------|-----------------|
| **Unit tests** (doctest) | 79 tests, ASan + UBSan in CI | Logic bugs, edge cases |
| **Coverage** (clang llvm-cov) | 90.2% line / 81.7% branch | Untouched code paths |
| **Synthetic ITCH replay** | 5M events, byte-exact reconstruct | Parser ↔ reconstructor drift |
| **Differential vs liquibook** | 4 byte-equal cases; random stress finds 1 open divergence | Engine-vs-engine semantic disagreement |
| **libFuzzer** | 385M execs · 3.18M exec/s · 0 crashes | Out-of-bounds, signed overflow, alignment |
| **clang-tidy** | clean | Lint, modernize, bugprone, perf |
| **CI matrix** | linux + macOS × {gcc-13, clang-18} × {Debug+ASan/UBSan, Release} | Toolchain drift |

> An honest open item: random-stress differential against `liquibook`
> finds **one divergence** (~50k commands deep, specific seed) that
> hasn't been root-caused yet. The harness flagging it is the whole
> point of having a diff harness — surfacing it here, not hiding it,
> for the same reason. See
> [`docs/diff-test-schema.md`](docs/diff-test-schema.md).

## Roadmap to v1.0

| Status | Item |
|--------|------|
| done | ITCH 5.0 parser, types, build system, CI |
| done | Order pool, intrusive book |
| done | CDA matcher (Limit / Market / IOC / FOK / cancel / modify) |
| done | Book reconstruction |
| done | Latency benchmarks |
| done | FBA mode |
| done | Agent simulator + FBA paper reproduction sweep |
| done | Differential test scaffold |
| done | SPSC ring + OUCH structural types |
| done | Power scaffold (Linux RAPL) |
| done | Fuzz harness — 121 s / 385M executions, 0 crashes |
| done | liquibook vendored + driver (4 byte-equal, 1 known divergence) |
| done | Synthetic ITCH 5.0 replay validation (5M events byte-exact) |
| done | clang-tidy clean, 90% line coverage |
| wip  | Real NASDAQ ITCH 5.0 sample-day replay (needs vendor data) |
| wip  | Triage and fix the diff_validate divergence |
| wip  | io_uring net thread (Linux) |
| wip  | Multi-symbol support |
| wip  | OUCH wire encoder/decoder |
| wip  | Linux x86 TSC + RAPL bench run (script ready, needs Linux host) |
| wip  | 24-hour fuzz run on Linux |

## Requirements

- CMake ≥ 3.24
- C++23 compiler (clang 17+ / gcc 13+ / AppleClang 15+)
- Linux preferred for benchmarks; macOS supported for development
- POSIX threads (used by the SPSC test and any threaded driver code)

## Documentation

| Document | What's in it |
|----------|--------------|
| [`docs/architecture.md`](docs/architecture.md) | Per-component implementation map and hot-path walkthrough |
| [`DESIGN.md`](DESIGN.md) | Design *rationale* — why an array vs `std::map`, why a pool, why three threads |
| [`docs/methodology.md`](docs/methodology.md) | Exact procedure for the latency, power, replay, FBA-sweep, and diff benchmarks |
| [`docs/diff-test-schema.md`](docs/diff-test-schema.md) | Canonical fill schema and v0.3.0 known divergence |
| [`docs/fba-paper-reproduction.md`](docs/fba-paper-reproduction.md) | What we reproduced from Budish-Cramton-Shim and what we didn't |
| [`docs/fuzz-results.md`](docs/fuzz-results.md) | Fuzz run log |
| [`CHANGELOG.md`](CHANGELOG.md) | Versioned summary of what landed when |

## License

MIT. See [LICENSE](LICENSE).
