#include <lazerbook/fba.hpp>

#include <cstdint>
#include <vector>

#include <cassert>

namespace lazerbook {

FbaMatcher::FbaMatcher(Book& book, OrderPool& pool, EventSink& sink, std::uint64_t seed)
    : book_(book), pool_(pool), sink_(sink), seed_(seed) {}

void FbaMatcher::on_new(OrderId id, Side side, Price4 price, std::uint32_t shares) {
    if (shares == 0) {
        sink_.on_rejected(OrderRejected{id, RejectReason::InvalidShares});
        return;
    }
    if (resting_.contains(value_of(id))) {
        sink_.on_rejected(OrderRejected{id, RejectReason::DuplicateOrderId});
        return;
    }
    if (!book_.in_range(price)) {
        sink_.on_rejected(OrderRejected{id, RejectReason::PriceOutOfRange});
        return;
    }
    Order* o = pool_.acquire();
    if (o == nullptr) {
        sink_.on_rejected(OrderRejected{id, RejectReason::PoolExhausted});
        return;
    }
    o->id = id;
    o->price = price;
    o->shares = shares;
    o->side = side;
    book_.add(o);
    resting_.emplace(value_of(id), o);
    sink_.on_accepted(OrderAccepted{id, side, price, shares});
}

void FbaMatcher::on_cancel(OrderId id) {
    auto it = resting_.find(value_of(id));
    if (it == resting_.end()) {
        sink_.on_rejected(OrderRejected{id, RejectReason::OrderNotFound});
        return;
    }
    Order* o = it->second;
    std::uint32_t const remaining = o->shares;
    book_.remove(o);
    pool_.release(o);
    resting_.erase(it);
    sink_.on_cancelled(OrderCancelled{id, remaining});
}

std::uint64_t FbaMatcher::clear() {
    ++batch_number_;
    std::uint32_t const lo = value_of(book_.min_price());
    std::uint32_t const n = book_.num_ticks();

    // Per-tick resting volume on each side.
    std::vector<std::uint64_t> bid_at(n, 0);
    std::vector<std::uint64_t> ask_at(n, 0);
    for (std::uint32_t i = 0; i < n; ++i) {
        Price4 const px{lo + i};
        if (PriceLevel const* b = book_.level_at(px, Side::Buy)) {
            bid_at[i] = b->total_shares;
        }
        if (PriceLevel const* a = book_.level_at(px, Side::Sell)) {
            ask_at[i] = a->total_shares;
        }
    }

    // demand(px) = bids at price >= px (suffix); supply(px) = asks at price <= px (prefix).
    std::vector<std::uint64_t> demand(n, 0);
    std::vector<std::uint64_t> supply(n, 0);
    std::uint64_t run = 0;
    for (std::uint32_t i = n; i-- > 0;) {
        run += bid_at[i];
        demand[i] = run;
    }
    run = 0;
    for (std::uint32_t i = 0; i < n; ++i) {
        run += ask_at[i];
        supply[i] = run;
    }

    // Walrasian interval: prices achieving the max clearing volume.
    std::uint64_t best_vol = 0;
    std::uint32_t lo_idx = 0;
    std::uint32_t hi_idx = 0;
    bool found = false;
    for (std::uint32_t i = 0; i < n; ++i) {
        std::uint64_t const v = std::min(demand[i], supply[i]);
        if (v > best_vol) {
            best_vol = v;
            lo_idx = i;
            hi_idx = i;
            found = true;
        } else if (found && v == best_vol && v > 0) {
            hi_idx = i;
        }
    }
    if (best_vol == 0) {
        return 0;
    }
    // Clearing price P* = midpoint of the Walrasian interval (tie-break).
    Price4 const clearing_price{lo + ((lo_idx + hi_idx) / 2U)};
    // Marginal price must collapse to a single tick before pro-rata.
    assert(hi_idx == lo_idx && "walrasian interval spans multiple ticks");

    // Greedy price-time fills inside the cleared volume. The choice of P* sets
    // only the trade price, not who trades, so greedy crossing is always safe.
    // seed_ ⊕ batch_number_ tags the deterministic ordering of this batch.
    std::uint64_t const order_tag = seed_ ^ batch_number_;
    (void)order_tag;

    std::uint64_t volume = 0;
    while (!book_.empty(Side::Buy) && !book_.empty(Side::Sell)) {
        Price4 const bb = book_.best_bid();
        Price4 const ba = book_.best_ask();
        if (value_of(bb) < value_of(ba)) {
            break;  // no remaining cross
        }
        PriceLevel* buy_lvl = book_.level_at(bb, Side::Buy);
        PriceLevel* sell_lvl = book_.level_at(ba, Side::Sell);
        Order* buy = buy_lvl->head;
        Order* sell = sell_lvl->head;
        std::uint32_t const qty = std::min(buy->shares, sell->shares);

        sink_.on_fill(Fill{buy->id, sell->id, clearing_price, qty, Side::Buy});
        volume += qty;

        if (qty == buy->shares) {
            resting_.erase(value_of(buy->id));
            book_.remove(buy);
            pool_.release(buy);
        } else {
            book_.reduce(buy, qty);
        }
        if (qty == sell->shares) {
            resting_.erase(value_of(sell->id));
            book_.remove(sell);
            pool_.release(sell);
        } else {
            book_.reduce(sell, qty);
        }
    }
    return volume;
}

}  // namespace lazerbook
