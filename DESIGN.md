# lazerbook — Design Notes

This document captures the *why* behind design choices. Living doc;
updated whenever a non-obvious decision is made. For a *what's wired
to what* implementation map, see
[`docs/architecture.md`](docs/architecture.md).

## 0. What shipped — v0.3.0 (2026-06-09)

Snapshot of which choices below have been validated in code, so the
rationale sections that follow can be read against reality:

- ✅ §3 book layout — price-indexed array + intrusive list, in
  `book.hpp` and exercised under random stress.
- ✅ §4 allocation — `OrderPool` + freelist, no `new`/`malloc` on the
  hot path; verified leak-free under ASan.
- ✅ §5 threading — three-thread model and Vyukov SPSC ring landed
  (`spsc_ring.hpp`); `io_uring` net thread is the remaining piece.
- ✅ §7 CDA — Limit / Market / IOC / FOK + cancel + modify with
  strict price-time priority.
- ✅ §8 FBA — uniform-price discrete clearing; deterministic
  pro-rata at the marginal price.
- ✅ §9 differential testing — `liquibook` vendored, `LiquibookDriver`
  wired, 4 byte-equal cases; random stress correctly surfaces one
  open divergence (triage open, see
  [`docs/diff-test-schema.md`](docs/diff-test-schema.md)).
- ✅ §10 power — Intel RAPL path landed (Linux x86 host run still
  pending a real bench machine).
- ✅ §11 determinism — egress hash + reproducible FBA pro-rata.
- ✅ Validation moat beyond what this doc lists: 79 doctest unit
  tests, 90.2% line / 81.7% branch coverage, libFuzzer 385M execs
  / 0 crashes, 5M-event synthetic ITCH replay byte-exact, FBA paper
  reproduction sweep (Sniper P&L flips +12.8M → −112M as predicted).
- 🟡 §12 deferred items still deferred: multi-symbol, FIX, iceberg,
  TLS, replication. Plus the v1.0-roadmap items in the README.

## 1. Goals (recap)

- Sub-microsecond p99 limit-order insertion latency, single-threaded, single-core.
- Bit-exact ITCH 5.0 reconstruction.
- Both CDA and FBA matching modes from one core.
- Byte-exact differential parity with `liquibook` on real replays.
- Defensible power-efficiency methodology and numbers.

## 2. Language & toolchain

**C++23.** Concepts, `std::expected`, `std::span`, `if consteval`, deducing-this all pay for themselves. Older compilers (C++17/20) are explicitly out of scope — this is a portfolio project, not a library for the world.

**CMake ≥ 3.24** for `target_compile_features` and modern presets. Build presets land in week 1.

## 3. Book layout

The data structure is the headline performance decision. Three real options:

| Option | Insert | Cancel | Best price | Notes |
|--------|--------|--------|------------|-------|
| `std::map<Price, std::deque<Order>>` | O(log n) | O(log n) | O(1) | Cache-hostile. |
| Hash map + sorted price list | O(1) avg | O(1) | O(log n) | Worse for sweeps. |
| **Price-indexed array + intrusive doubly-linked list per level** | O(1) | O(1) | O(1) amort | Best for bounded tick range. |

We pick **option 3**.

## 4. Allocation

No `new`/`malloc` on the hot path. A pre-sized `OrderPool` hands out `Order*` from a freelist.

## 5. Threading

Three pinned threads:
- Net thread
- Matcher thread
- Egress thread

All inter-thread communication via cacheline-padded SPSC ring buffers.

## 6. Time

The matcher uses TSC (`__rdtsc` on x86) for internal sequence numbers and latency timestamps.

## 7. CDA semantics

Strict price-time priority. Limit/Market/IOC/FOK in v1.

## 8. FBA semantics (per Budish-Cramton-Shim)

- Batch window configurable, default 100 ms.
- Uniform-price max-volume clearing.
- Deterministic remainder allocation.

## 9. Differential testing

`liquibook` is the reference.

## 10. Power benchmarking

Intel RAPL methodology.

## 11. Determinism

Run-to-run: identical input → identical output bytes.

## 12. Non-decisions (deferred)

- Multi-symbol
- Cross-symbol orders
- FIX gateway
- Replication / consensus
- TLS / auth
- Web UI / visualizer
