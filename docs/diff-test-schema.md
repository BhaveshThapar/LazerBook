# Differential test schema

lazerbook is tested for *economic* equivalence against the vendored `liquibook`
engine. The contract is the `CanonicalFill`.

## `CanonicalFill`

```cpp
struct CanonicalFill {
    std::uint64_t aggressor_id;
    std::uint64_t passive_id;
    std::uint32_t price_ticks;
    std::uint32_t shares;
    Side          aggressor_side;
};
```

Two engines are equal iff they emit the same sequence of `CanonicalFill`s for the
same command stream.

## What is intentionally absent

- **Timestamps** — engines clock fills differently; the economic outcome does not
  depend on wall-clock.
- **Match numbers / sequence numbers** — exchange-assigned identifiers that differ
  per engine and carry no economic meaning.
- **Accepts / cancels / rejects** — only fills are compared; the book-state
  consequences of accepts/cancels show up in subsequent fills anyway.

## Command alphabet

`Command::Kind ∈ { NewLimit, NewMarket, NewIoc, NewFok, Cancel, Modify }`. The
`Modify` arm is cancel-replace in our semantics; the liquibook driver emulates
cancel-replace rather than liquibook's in-place `replace` (which preserves time
priority) so the two engines are compared on the same rule.

## Driver contract

A driver is any callable `void(span<Command const>, vector<CanonicalFill>& out)`.
`run_differential(commands, a, b, out_a, out_b)` runs both, compares fill-by-fill,
and returns a `DiffResult { equal, divergent_at, commands_processed, fills_a,
fills_b }`, stopping at the first differing fill.

## Divergence-dump procedure

`bench/diff_validate` prints the divergent fill index and the ±100 surrounding
fills from each engine so a divergence can be reduced to a minimal command
sequence by hand.

## Current status

- **4 / 4** hand-written cases (`test_diff_liquibook.cpp`: simple cross, level
  walk, time priority, cancel-then-trade) are byte-equal.
- **1 known divergence** surfaces in the 100k random-stress `diff_validate` at
  ~50k commands on a specific seed. It is surfaced deliberately — narrowing it is
  tracked for v1.0.

## v1.0 coverage targets

Extend the byte-equal set to cover market and FOK order types end-to-end, and
drive the random-stress harness to zero divergences (or a documented,
rule-level explanation for any remaining one).
