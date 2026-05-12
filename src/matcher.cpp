#include <lazerbook/matcher.hpp>

namespace lazerbook {

Matcher::Matcher(Book& book, OrderPool& pool, EventSink& sink)
    : book_(book), pool_(pool), sink_(sink) {}

bool Matcher::crosses(Side aggressor, Price4 limit, Price4 resting_px, bool unbounded)
    const noexcept {
    if (unbounded) {
        return true;
    }
    if (aggressor == Side::Buy) {
        return value_of(limit) >= value_of(resting_px);
    }
    return value_of(limit) <= value_of(resting_px);
}

bool Matcher::can_fully_fill(Side side, Price4 limit, std::uint32_t shares) const noexcept {
    std::uint64_t avail = 0;
    Side const opp = (side == Side::Buy) ? Side::Sell : Side::Buy;
    if (book_.empty(opp)) {
        return false;
    }
    if (side == Side::Buy) {
        // Walk asks up from best until past the limit.
        for (std::uint32_t px = value_of(book_.best_ask()); px <= value_of(limit); ++px) {
            PriceLevel const* lvl = book_.level_at(Price4{px}, Side::Sell);
            if (lvl != nullptr) {
                avail += lvl->total_shares;
                if (avail >= shares) {
                    return true;
                }
            }
        }
    } else {
        // Walk bids down from best until below the limit.
        std::uint32_t px = value_of(book_.best_bid());
        while (px + 1 > value_of(limit)) {  // px >= limit, underflow-safe
            PriceLevel const* lvl = book_.level_at(Price4{px}, Side::Buy);
            if (lvl != nullptr) {
                avail += lvl->total_shares;
                if (avail >= shares) {
                    return true;
                }
            }
            if (px == 0) {
                break;
            }
            --px;
        }
    }
    return avail >= shares;
}

std::uint32_t Matcher::match(
    OrderId id, Side side, Price4 limit, std::uint32_t shares, bool unbounded
) {
    std::uint32_t remaining = shares;
    Side const opp = (side == Side::Buy) ? Side::Sell : Side::Buy;

    while (remaining > 0 && !book_.empty(opp)) {
        Price4 const best = (side == Side::Buy) ? book_.best_ask() : book_.best_bid();
        if (!crosses(side, limit, best, unbounded)) {
            break;
        }
        PriceLevel* lvl = book_.level_at(best, opp);
        while (remaining > 0 && lvl->head != nullptr) {
            Order* passive = lvl->head;
            std::uint32_t const qty = std::min(remaining, passive->shares);
            sink_.on_fill(Fill{id, passive->id, best, qty, side});
            remaining -= qty;
            if (qty == passive->shares) {
                resting_.erase(value_of(passive->id));
                book_.remove(passive);  // refreshes best cursor when level empties
                pool_.release(passive);
            } else {
                book_.reduce(passive, qty);
            }
        }
    }
    return remaining;
}

void Matcher::rest(OrderId id, Side side, Price4 price, std::uint32_t shares) {
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

void Matcher::on_new(OrderId id, Side side, OrderType type, Price4 price, std::uint32_t shares) {
    if (shares == 0) {
        sink_.on_rejected(OrderRejected{id, RejectReason::InvalidShares});
        return;
    }
    if (resting_.contains(value_of(id))) {
        sink_.on_rejected(OrderRejected{id, RejectReason::DuplicateOrderId});
        return;
    }
    bool const is_market = (type == OrderType::Market);
    if (!is_market && !book_.in_range(price)) {
        sink_.on_rejected(OrderRejected{id, RejectReason::PriceOutOfRange});
        return;
    }

    switch (type) {
        case OrderType::Fok: {
            if (!can_fully_fill(side, price, shares)) {
                sink_.on_rejected(OrderRejected{id, RejectReason::FokWouldNotFullyFill});
                return;
            }
            match(id, side, price, shares, /*unbounded=*/false);
            return;
        }
        case OrderType::Market: {
            match(id, side, price, shares, /*unbounded=*/true);
            return;  // never rests
        }
        case OrderType::Ioc: {
            match(id, side, price, shares, /*unbounded=*/false);
            return;  // never rests
        }
        case OrderType::Limit: {
            std::uint32_t const remaining = match(id, side, price, shares, /*unbounded=*/false);
            if (remaining > 0) {
                rest(id, side, price, remaining);
            }
            return;
        }
    }
}

void Matcher::on_cancel(OrderId id) {
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

void Matcher::on_modify(
    OrderId old_id, OrderId new_id, Price4 new_price, std::uint32_t new_shares
) {
    auto it = resting_.find(value_of(old_id));
    if (it == resting_.end()) {
        sink_.on_rejected(OrderRejected{new_id, RejectReason::OrderNotFound});
        return;
    }
    Order* o = it->second;
    Side const side = o->side;
    std::uint32_t const remaining = o->shares;
    book_.remove(o);
    pool_.release(o);
    resting_.erase(it);
    sink_.on_cancelled(OrderCancelled{old_id, remaining});
    // Cancel-replace loses time priority and may cross on re-entry.
    on_new(new_id, side, OrderType::Limit, new_price, new_shares);
}

}  // namespace lazerbook
