# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.3.0] - 2026-06-09

### Added
- Synthetic-replay validator (`replay_validate`): synth → parse → reconstruct,
  asserting reconstructed book aggregates equal the synth ground truth at 5M
  events.
- `liquibook` integration (`LAZERBOOK_WITH_LIQUIBOOK=ON`): 4 hand-written
  byte-equal differential cases plus a 100k random-stress `diff_validate`.
- libFuzzer harness (`fuzz_itch`) with a synth-seeded corpus (`seed_fuzz_corpus`).
- FBA paper-reproduction sweep (`fba_sweep`) emitting CSV across batch windows.
- Coverage tooling (`tools/coverage.sh`) and a reproducible Linux/x86 bench
  recipe (`tools/bench-linux.sh`).

### Changed
- clang-tidy clean across the tree; coverage measured at 90.2% line / 81.7%
  branch on Linux/clang-18.

### Fixed
- FBA clearing edge case: replaced the special-cased marginal-price pro-rata
  allocation with greedy price-time fills inside the cleared volume after a
  Debug-only assertion surfaced a Walrasian-interval corner case. `P*` sets only
  the trade price, not the trading set, so greedy fills are always safe.

## [0.2.0] - 2026-05-22

### Added
- CDA matcher with strict price-time priority and Limit/Market/IOC/FOK order
  types, cancel, and cancel-replace.
- FBA uniform-price batch-auction matcher on the shared engine core.
- ITCH reconstructor and coherent ITCH stream generator with ground-truth
  tracking.
- Tiny multi-agent simulator (MarketMaker / Sniper / ZeroIntelligence) with CDA
  and FBA run functions.
- Vyukov SPSC ring, OUCH 4.2 structural types, differential-test harness
  scaffold, bench scaffolding, and a libFuzzer harness stub.

## [0.1.0] - 2026-05-01

### Added
- Project scaffold: CMake build, warning/sanitizer/coverage interface libraries,
  clang-format/clang-tidy config, CI matrix.
- Foundation types (`types.hpp`) with big-endian codecs.
- NASDAQ TotalView-ITCH 5.0 parser covering 22 message types.

[0.3.0]: https://example.com/lazerbook/releases/tag/v0.3.0
[0.2.0]: https://example.com/lazerbook/releases/tag/v0.2.0
[0.1.0]: https://example.com/lazerbook/releases/tag/v0.1.0
