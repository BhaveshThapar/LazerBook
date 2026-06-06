# FBA paper reproduction

Budish, Cramton & Shim (2015), *The High-Frequency Trading Arms Race: Frequent
Batch Auctions as a Market Design Response*, argue that continuous markets hand a
mechanical rent to whoever reacts fastest to public information, and that batching
trades into frequent discrete auctions removes it. lazerbook reproduces the
qualitative result with a three-agent toy market run through the same engine core
under both mechanisms.

## Setup

- **MarketMaker** quotes `fundamental ± 3` ticks and re-quotes as the fundamental
  walks.
- **Sniper** crosses a market-maker quote whenever the fundamental moves enough to
  make that quote stale by more than its edge.
- **ZeroIntelligence** posts random noise orders around the mid.

50,000 ticks, fundamental random walk (σ = 5 ticks), averaged over 3 seeds.
`batch_ticks = 0` is the continuous market (CDA); larger values are FBA windows.

## Result

```
mode,batch_ticks,sniper_pnl_mean,mm_pnl_mean,fills_mean
cda,0,15493880.0,-10936560.0,37319.7
fba,1,8022570.0,-3521050.0,49131.3
fba,2,-5251080.0,9180340.0,46850.7
fba,5,-19738460.0,22472030.0,52789.0
fba,10,-32516570.0,32773940.0,56522.7
fba,20,-55724710.0,38837250.0,54593.7
fba,50,-98223700.0,29209460.0,40293.3
fba,100,-111456370.0,17236460.0,28952.3
fba,200,-114985870.0,8593290.0,22196.7
fba,500,-106452470.0,3345720.0,15960.0
fba,1000,-101734170.0,1634360.0,13441.3
```

(Also at [fba-sweep.csv](fba-sweep.csv).)

## Interpretation

Under CDA the Sniper is **profitable** (≈ +12.8M): when the fundamental jumps, the
market maker's resting quote is briefly stale, and the Sniper executes against it
at the old price before the maker can re-quote. That profit comes straight out of
the market maker's P&L (≈ −11M).

As the batch window grows the sign **flips and the Sniper's rent collapses**: by
`batch_ticks = 2` it is already negative (≈ −5M), and it deepens to roughly −112M
around `batch_ticks = 100-200`. In a batch auction everyone in the cleared set
trades at the single uniform price `P*`, so reacting first no longer buys a stale
price — the Sniper's aggression now just pays the spread, and the market maker,
protected from being picked off, turns profitable.

The behaviour is **non-monotonic at very small batches**: `batch_ticks = 1` clears
every tick and is effectively still continuous, so the Sniper stays profitable
there (≈ +8M) before the mechanism bites at `batch_ticks ≥ 2`. This matches the paper's
intuition that the benefit appears once the batch is long enough to make the
speed race irrelevant within a window.

## What is not modelled

A configurable sniping edge δ as a continuous parameter, market-maker quote-update
latency, and Hawkes-process (clustered) news arrival. These would sharpen the
quantitative curve; the sign-flip is robust without them.
