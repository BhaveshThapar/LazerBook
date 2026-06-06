# Architecture

This doc covers **how lazerbook is wired together** at v0.3.0 — the
components, how they communicate, and what the hot path looks like.
Design *rationale* (why an array, why a pool, why three threads) lives
in [`../DESIGN.md`](../DESIGN.md). The two docs are complementary;
this one assumes the rationale is settled and walks the implementation.

## At a glance

```
                   ┌────────────────────────────────────────────────┐
                   │  Three pinned threads, no locks on the hot path │
                   └────────────────────────────────────────────────┘

  ┌──────────────┐    SPSC    ┌─────────────────────┐    SPSC    ┌──────────────┐
  │  Net thread  │ ─────────▶ │   Matcher thread    │ ─────────▶ │ Egress thread│
  │              │            │  (single owner of   │            │              │
  │  io_uring /  │            │   Book + OrderPool) │            │  serialises  │
  │  kqueue      │            │                     │            │  ITCH-style  │
  │  parses OUCH │            │  ┌──────┬───────┐   │            │  market data │
  └──────────────┘            │  │ CDA  │  FBA  │   │            └──────────────┘
                              │  └──────┴───────┘   │
                              └─────────────────────┘
       core 2 (pinned)              core 4 (pinned,                core 6 (pinned)
                                    sibling SMT idle)
```

- **Net** owns parsing (OUCH inbound, ITCH replay) and lifts bytes into
  `Command` records.
- **Matcher** is the only thread that touches the `Book` and the
  `OrderPool` — single ownership, so no locks.
- **Egress** drains the result queue and serialises market data to
  whoever's listening.
- The two arrows are **Vyukov-style cacheline-padded SPSC ring buffers**
  (`include/lazerbook/spsc_ring.hpp`).

CDA vs FBA is selected at startup; both share the same `Book`,
`OrderPool`, and threading layout. Only the fill-generation policy
changes.

## Component map (v0.3.0)

| Component | Header | Source | Owner | Role |
|-----------|--------|--------|-------|------|
| Types & big-endian decode | [`types.hpp`](../include/lazerbook/types.hpp) | — | foundation | `Price4`, `OrderId`, `Side`, `Symbol`; raw-byte decoders |
| ITCH 5.0 parser | [`itch.hpp`](../include/lazerbook/itch.hpp) | [`src/itch.cpp`](../src/itch.cpp) | Net | Decodes 22 NASDAQ TotalView-ITCH 5.0 message types |
| ITCH synthesiser | [`itch_synth.hpp`](../include/lazerbook/itch_synth.hpp) | [`src/itch_synth.cpp`](../src/itch_synth.cpp) | offline | Generates a coherent ITCH 5.0 byte stream for tests / fuzz seeds |
| Order POD | [`order.hpp`](../include/lazerbook/order.hpp) | — | Matcher | 40-byte POD with intrusive `prev`/`next` |
| Order pool | [`order_pool.hpp`](../include/lazerbook/order_pool.hpp) | — | Matcher | Pre-allocated freelist; O(1) pop/push |
| Book | [`book.hpp`](../include/lazerbook/book.hpp) | [`src/book.cpp`](../src/book.cpp) | Matcher | Per-side price-indexed array of intrusive lists |
| Events | [`events.hpp`](../include/lazerbook/events.hpp) | — | Matcher → Egress | `Fill` / `OrderAccepted` / `OrderCancelled` / `OrderRejected` |
| CDA matcher | [`matcher.hpp`](../include/lazerbook/matcher.hpp) | [`src/matcher.cpp`](../src/matcher.cpp) | Matcher | Limit / Market / IOC / FOK + cancel + modify, strict price-time priority |
| FBA matcher | [`fba.hpp`](../include/lazerbook/fba.hpp) | [`src/fba.cpp`](../src/fba.cpp) | Matcher | Uniform-price discrete batch auction (Budish-Cramton-Shim) |
| Reconstructor | [`reconstruct.hpp`](../include/lazerbook/reconstruct.hpp) | [`src/reconstruct.cpp`](../src/reconstruct.cpp) | offline | Applies ITCH events to a `Book` for L2/L3 replay |
| Simulator | [`sim.hpp`](../include/lazerbook/sim.hpp) | [`src/sim.cpp`](../src/sim.cpp) | offline | Sniper / MarketMaker / ZI agents; drives `fba_compare` and `fba_sweep` |
| SPSC ring | [`spsc_ring.hpp`](../include/lazerbook/spsc_ring.hpp) | — | Net↔Matcher↔Egress | Vyukov SPSC, cacheline-padded, lock-free |
| OUCH 4.2 types | [`ouch.hpp`](../include/lazerbook/ouch.hpp) | — | Net | Structural shapes only; wire encoder pending |
| Diff canonical schema | [`diff/canonical.hpp`](../include/lazerbook/diff/canonical.hpp) | — | offline | 24-byte `CanonicalFill` for engine-vs-engine compare |
| Diff harness | [`diff/harness.hpp`](../include/lazerbook/diff/harness.hpp) | — | offline | `run_differential` runs two drivers, asserts byte-equal fill streams |
| lazerbook driver | [`diff/lazerbook_driver.hpp`](../include/lazerbook/diff/lazerbook_driver.hpp) | — | offline | Wraps our `Matcher` |
| liquibook driver | [`diff/liquibook_driver.hpp`](../include/lazerbook/diff/liquibook_driver.hpp) | — | offline | Wraps `liquibook::book::OrderBook`; gated by `LAZERBOOK_WITH_LIQUIBOOK=ON` |

## Hot path: limit-order insert

What happens between an incoming OUCH-encoded order and a `Fill` on
the wire. The annotated steps below are the same six steps the
`bench_matcher` tool isolates and times.

```
  bytes  ──▶  [1 parse]  ──▶  Command  ──SPSC──▶  Matcher
                                                     │
                                                     ▼
                                              [2 OrderPool::pop]
                                                     │
                                                     ▼
                                              [3 list push at levels[tick]]
                                                     │
                                                     ▼
                                              [4 hash insert id → Order*]
                                                     │
                                                     ▼
                                              [5 match loop on opposite side]
                                                     │
                                                     ▼
                                              [6 SPSC publish events]
                                                     │
                                                     ▼
                                                  Egress
```

1. **Parse.** `itch::parse` decodes the wire bytes into a typed
   `Command` (the engine's internal command stream — same shape
   whether it came from OUCH inbound, ITCH replay, or the simulator).
2. **Pool pop.** `OrderPool::pop()` returns a pointer to a 40-byte
   `Order` slot from a pre-sized freelist. O(1), no allocation, no
   syscall.
3. **List push.** The `Order` is linked at the tail of the doubly-linked
   list at `levels[tick_offset]` via its intrusive `prev`/`next`
   pointers — no extra node allocation.
4. **Hash insert.** `unordered_map<OrderId, Order*>::emplace` so we
   can later cancel by id in O(1).
5. **Match loop.** Walk the opposite side's price levels in
   priority order; for each crossing resting order, generate a `Fill`
   event and decrement quantities. Strict price-time priority for CDA;
   for FBA, this step is deferred to the batch boundary and the
   uniform-price auction is run instead (`fba.cpp:run_auction`).
6. **Publish.** Fill / OrderAccepted / OrderCancelled / OrderRejected
   events are pushed to the egress SPSC ring.

Nothing on this path calls `new`, takes a lock, or makes a syscall.

## Threading

- Each thread is `pthread_setaffinity_np`'d to a non-boot CPU; the
  matcher's sibling SMT thread is left idle to avoid execution-port
  contention.
- The two SPSC rings are the only inter-thread communication. Head
  and tail cursors live on **separate cache lines** (64 B padding)
  — without this, a producer write to the tail invalidates the
  consumer's cached copy of the head and turns the ring into a
  cacheline ping-pong.
- The matcher is the **single owner** of the `Book` and `OrderPool`,
  which is why no locks appear at this layer at all.
- See [`../DESIGN.md`](../DESIGN.md) §5 for why this layout (vs.
  per-symbol sharding, vs. work-stealing) was chosen.

## Book layout

Per side, a contiguous `std::array<PriceLevel, N>` indexed by tick
offset from a base price, where each `PriceLevel` is the head of a
doubly-linked list of `Order` PODs.

```
  bid side                                         ask side
  ┌────┬────┬────┬────┬────┐                       ┌────┬────┬────┬────┬────┐
  │    │    │ ●  │ ●  │ ●  │  ◀ best bid    best ask ▶  │ ●  │ ●  │    │    │
  └────┴────┴─┬──┴─┬──┴─┬──┘                       └─┬──┴─┬──┴────┴────┴────┘
              │    │    │                            │    │
              ▼    ▼    ▼                            ▼    ▼
            list  list list                        list  list
            ┌──┐  ┌──┐ ┌──┐                        ┌──┐  ┌──┐
            │O9│  │O7│ │O5│                        │O5│  │O8│
            ├──┤       ├──┤                        ├──┤
            │O3│       │O2│                        │O2│
            ├──┤       └──┘                        └──┘
            │O1│
            └──┘
```

Each `Order` is its own list node; cancel-by-id is one hash lookup
(O(1)) plus one list unlink (O(1)). Best-bid / best-ask cursors are
kept up to date inside the matcher so price-discovery is also O(1)
amortised.

The bounded-tick assumption (NASDAQ liquid names span <1024 ticks
intraday) makes the array small enough to live in L1. Wide-tick
instruments are out of scope for v1; see [`../DESIGN.md`](../DESIGN.md)
§3 for the deferral note.

## CDA vs FBA

Both modes share the `Book`, `OrderPool`, and SPSC layout. They
differ only in *when and how fills are generated*.

| Aspect | CDA (default) | FBA |
|--------|---------------|-----|
| Trigger | Every incoming order | Batch window boundary (default 100 ms) |
| Order semantics | Limit / Market / IOC / FOK | Limit only (other types deferred) |
| Priority | Strict price-time | Uniform clearing price; greedy price-time fills within cleared volume |
| Determinism source | Arrival order | Batch number ⊕ instance seed for marginal-price tie-break |
| Code | `matcher.cpp` | `fba.cpp` |

The FBA clearing rule maximises `min(cum_bid_volume, cum_ask_volume)`
across candidate prices; the marginal price is the value that achieves
this. Within the cleared volume, fills are generated greedily by
price-time priority — see [`../CHANGELOG.md`](../CHANGELOG.md) v0.3.0
for the edge case that drove this simplification.

## Offline tooling

These run outside the runtime threading model — they're test and
research drivers that share the engine's components.

- **`bench/bench_matcher`** — latency benchmark: insert / insert+match
  / cancel scenarios, p50 / p99 / p99.9 / p99.99 / max over 100k samples.
  TSC on x86, `steady_clock` elsewhere. See
  [`methodology.md`](methodology.md).
- **`bench/bench_power`** — wraps `bench_matcher` with Intel RAPL
  energy snapshots; reports joules per million matches. Linux/Intel
  only.
- **`bench/replay_validate`** — drives N synthesised ITCH events
  through the parser + reconstructor, asserts byte-exact match against
  the synth's ground truth. v0.3.0 run: 5M events, byte-exact.
- **`bench/fba_compare`** — single agent population, both modes,
  side-by-side P&L.
- **`bench/fba_sweep`** — `fba_compare` averaged over 3 seeds × 11
  batch sizes; output is [`fba-sweep.csv`](fba-sweep.csv). See
  [`fba-paper-reproduction.md`](fba-paper-reproduction.md).
- **`bench/diff_validate`** — random command stream through both
  `LazerbookDriver` and `LiquibookDriver`, byte-equal assertion. See
  [`diff-test-schema.md`](diff-test-schema.md).
- **`bench/fuzz_itch`** — libFuzzer harness over `itch::parse`;
  ASan + UBSan. v0.3.0 run: 385M execs, 0 crashes. See
  [`fuzz-results.md`](fuzz-results.md).

## Determinism guarantees

These are non-negotiable — differential testing depends on them.

- **Run-to-run:** identical input → identical output bytes. The
  egress stream is hashed at the end of long-running tests.
- **Cross-platform** for the engine layer (not for the wall-clock
  numbers): same `Command` stream produces the same `Fill` stream on
  Linux x86, macOS Apple Silicon, and any other supported host.
- **FBA pro-rata at the marginal price** is seeded by
  `batch_number ⊕ instance_seed`, so the same batch on the same
  instance produces the same allocation every time.

## Build configuration

| Flag | Default | Effect |
|------|---------|--------|
| `CMAKE_BUILD_TYPE=Release` | required for benches | `-O3 -DNDEBUG` |
| `LAZERBOOK_BUILD_BENCH=ON` | off | builds `bench/*` targets |
| `LAZERBOOK_BUILD_FUZZ=ON` | off | builds `fuzz_itch`; clang-only |
| `LAZERBOOK_WITH_LIQUIBOOK=ON` | off | enables `LiquibookDriver` and `diff_validate` |
| `LAZERBOOK_COVERAGE=ON` | off | clang source-based coverage instrumentation |

CI runs Linux + macOS × {gcc-13, clang-18} × {Debug+ASan/UBSan,
Release}.

## What's deferred

Captured here for completeness; see the README's **Roadmap to v1.0**
for current status and [`../DESIGN.md`](../DESIGN.md) §12 for the
rationale on each.

- Multi-symbol (sharded across N matcher threads).
- `io_uring` net thread on Linux.
- OUCH 4.2 wire encoder/decoder.
- Iceberg / stop orders.
- AMD RAPL path.
- Real NASDAQ ITCH 5.0 sample-day replay (needs vendor data).
- Triage of the known `diff_validate` divergence.
- 24-hour fuzz run on Linux.

## Where to read next

- [`../DESIGN.md`](../DESIGN.md) — design rationale for every choice
  above.
- [`methodology.md`](methodology.md) — exact procedure for the
  latency, power, replay, FBA-sweep, and diff benchmarks.
- [`diff-test-schema.md`](diff-test-schema.md) — canonical fill schema
  and the v0.3.0 known divergence.
- [`fba-paper-reproduction.md`](fba-paper-reproduction.md) — what we
  reproduced from Budish-Cramton-Shim 2015 and what we didn't.
- [`fuzz-results.md`](fuzz-results.md) — fuzz run log.
