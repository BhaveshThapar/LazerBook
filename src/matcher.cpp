#include <lazerbook/matcher.hpp>

namespace lazerbook {

Matcher::Matcher(Book& book, OrderPool& pool, EventSink& sink)
    : book_(book), pool_(pool), sink_(sink), resting_(pool.capacity() * 2) {}

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
    // Hops between populated levels via the occupancy bitmap rather than
    // probing every tick between the touch and the limit -- the second
    // O(num_ticks) walk the book used to do, on the FOK path.
    Price4 const best = (side == Side::Buy) ? book_.best_ask() : book_.best_bid();
    std::uint32_t idx = book_.next_level_idx(book_.index_of_price(best), opp);

    while (idx != Book::kInvalidIdx) {
        Price4 const px = book_.price_at_index(idx);
        if (!crosses(side, limit, px, /*unbounded=*/false)) {
            break;
        }
        avail += book_.level_by_index(idx, opp).total_shares;
        if (avail >= shares) {
            return true;
        }
        if (side == Side::Buy) {
            if (idx + 1 >= book_.num_ticks()) {
                break;
            }
            idx = book_.next_level_idx(idx + 1, opp);
        } else {
            if (idx == 0) {
                break;
            }
            idx = book_.next_level_idx(idx - 1, opp);
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
                resting_.erase(passive->id);
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
    resting_.insert(id, o);
    sink_.on_accepted(OrderAccepted{id, side, price, shares});
}

void Matcher::on_new(OrderId id, Side side, OrderType type, Price4 price, std::uint32_t shares) {
    if (shares == 0) {
        sink_.on_rejected(OrderRejected{id, RejectReason::InvalidShares});
        return;
    }
    if (resting_.contains(id)) {
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
    Order* o = resting_.find(id);
    if (o == nullptr) {
        sink_.on_rejected(OrderRejected{id, RejectReason::OrderNotFound});
        return;
    }
    std::uint32_t const remaining = o->shares;
    book_.remove(o);
    pool_.release(o);
    resting_.erase(id);
    sink_.on_cancelled(OrderCancelled{id, remaining});
}

void Matcher::on_modify(
    OrderId old_id, OrderId new_id, Price4 new_price, std::uint32_t new_shares
) {
    Order* o = resting_.find(old_id);
    if (o == nullptr) {
        sink_.on_rejected(OrderRejected{new_id, RejectReason::OrderNotFound});
        return;
    }
    Side const side = o->side;
    std::uint32_t const remaining = o->shares;
    book_.remove(o);
    pool_.release(o);
    resting_.erase(old_id);
    sink_.on_cancelled(OrderCancelled{old_id, remaining});
    // Cancel-replace loses time priority and may cross on re-entry.
    on_new(new_id, side, OrderType::Limit, new_price, new_shares);
}

}  // namespace lazerbook
