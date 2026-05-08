#include <lazerbook/book.hpp>

namespace lazerbook {

Book::Book(Price4 min_price, std::uint32_t num_ticks)
    : min_price_(min_price),
      num_ticks_(num_ticks),
      bids_(num_ticks),
      asks_(num_ticks),
      best_bid_idx_(kInvalidIdx),
      best_ask_idx_(kInvalidIdx) {}

std::uint32_t Book::index_of(Price4 price) const noexcept {
    return value_of(price) - value_of(min_price_);
}

bool Book::in_range(Price4 price) const noexcept {
    std::uint32_t const p = value_of(price);
    std::uint32_t const lo = value_of(min_price_);
    return p >= lo && (p - lo) < num_ticks_;
}

PriceLevel* Book::level_at(Price4 price, Side side) noexcept {
    if (!in_range(price)) {
        return nullptr;
    }
    std::uint32_t const idx = index_of(price);
    return side == Side::Buy ? &bids_[idx] : &asks_[idx];
}

PriceLevel const* Book::level_at(Price4 price, Side side) const noexcept {
    if (!in_range(price)) {
        return nullptr;
    }
    std::uint32_t const idx = index_of(price);
    return side == Side::Buy ? &bids_[idx] : &asks_[idx];
}

void Book::add(Order* o) noexcept {
    std::uint32_t const idx = index_of(o->price);
    PriceLevel& lvl = (o->side == Side::Buy) ? bids_[idx] : asks_[idx];

    o->next = nullptr;
    o->prev = lvl.tail;
    if (lvl.tail != nullptr) {
        lvl.tail->next = o;
    } else {
        lvl.head = o;
    }
    lvl.tail = o;
    lvl.total_shares += o->shares;
    ++lvl.order_count;

    if (o->side == Side::Buy) {
        if (best_bid_idx_ == kInvalidIdx || idx > best_bid_idx_) {
            best_bid_idx_ = idx;
        }
    } else {
        if (best_ask_idx_ == kInvalidIdx || idx < best_ask_idx_) {
            best_ask_idx_ = idx;
        }
    }
}

void Book::remove(Order* o) noexcept {
    std::uint32_t const idx = index_of(o->price);
    PriceLevel& lvl = (o->side == Side::Buy) ? bids_[idx] : asks_[idx];

    if (o->prev != nullptr) {
        o->prev->next = o->next;
    } else {
        lvl.head = o->next;
    }
    if (o->next != nullptr) {
        o->next->prev = o->prev;
    } else {
        lvl.tail = o->prev;
    }
    lvl.total_shares -= o->shares;
    --lvl.order_count;
    o->prev = nullptr;
    o->next = nullptr;

    if (lvl.empty()) {
        if (o->side == Side::Buy && idx == best_bid_idx_) {
            refresh_best_bid_down(idx);
        } else if (o->side == Side::Sell && idx == best_ask_idx_) {
            refresh_best_ask_up(idx);
        }
    }
}

void Book::reduce(Order* o, std::uint32_t by) noexcept {
    if (by >= o->shares) {
        remove(o);
        return;
    }
    std::uint32_t const idx = index_of(o->price);
    PriceLevel& lvl = (o->side == Side::Buy) ? bids_[idx] : asks_[idx];
    o->shares -= by;
    lvl.total_shares -= by;
}

void Book::refresh_best_bid_down(std::uint32_t from_idx) noexcept {
    // Scan downward from the just-emptied best toward min_price.
    for (std::uint32_t i = from_idx; i-- > 0;) {
        if (!bids_[i].empty()) {
            best_bid_idx_ = i;
            return;
        }
    }
    best_bid_idx_ = kInvalidIdx;
}

void Book::refresh_best_ask_up(std::uint32_t from_idx) noexcept {
    for (std::uint32_t i = from_idx + 1; i < num_ticks_; ++i) {
        if (!asks_[i].empty()) {
            best_ask_idx_ = i;
            return;
        }
    }
    best_ask_idx_ = kInvalidIdx;
}

Price4 Book::best_bid() const noexcept {
    if (best_bid_idx_ == kInvalidIdx) {
        return Price4{0};
    }
    return Price4{value_of(min_price_) + best_bid_idx_};
}

Price4 Book::best_ask() const noexcept {
    if (best_ask_idx_ == kInvalidIdx) {
        return Price4{0};
    }
    return Price4{value_of(min_price_) + best_ask_idx_};
}

bool Book::empty(Side side) const noexcept {
    return side == Side::Buy ? best_bid_idx_ == kInvalidIdx : best_ask_idx_ == kInvalidIdx;
}

}  // namespace lazerbook
