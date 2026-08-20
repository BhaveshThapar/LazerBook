#include <lazerbook/book.hpp>

namespace lazerbook {

Book::Book(Price4 min_price, std::uint32_t num_ticks, std::uint32_t tick_size)
    : min_price_(min_price),
      num_ticks_(num_ticks),
      tick_size_((tick_size == 0) ? 1 : tick_size),
      bids_(num_ticks),
      asks_(num_ticks),
      bid_bits_(num_ticks),
      ask_bits_(num_ticks),
      best_bid_idx_(kInvalidIdx),
      best_ask_idx_(kInvalidIdx) {}

std::uint32_t Book::index_of(Price4 price) const noexcept {
    return (value_of(price) - value_of(min_price_)) / tick_size_;
}

bool Book::in_range(Price4 price) const noexcept {
    std::uint32_t const p = value_of(price);
    std::uint32_t const lo = value_of(min_price_);
    if (p < lo) {
        return false;
    }
    std::uint32_t const off = p - lo;
    // Off-grid prices have no slot. Rounding them would silently corrupt the
    // reconstruction, so they are out of range and get counted as such.
    return (off % tick_size_) == 0 && (off / tick_size_) < num_ticks_;
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
        bid_bits_.set(idx);
        if (best_bid_idx_ == kInvalidIdx || idx > best_bid_idx_) {
            best_bid_idx_ = idx;
        }
    } else {
        ask_bits_.set(idx);
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
        if (o->side == Side::Buy) {
            bid_bits_.clear(idx);
            if (idx == best_bid_idx_) {
                std::uint32_t const next = bid_bits_.highest();
                best_bid_idx_ = (next == TickBitmap::kNone) ? kInvalidIdx : next;
            }
        } else {
            ask_bits_.clear(idx);
            if (idx == best_ask_idx_) {
                std::uint32_t const next = ask_bits_.lowest();
                best_ask_idx_ = (next == TickBitmap::kNone) ? kInvalidIdx : next;
            }
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

std::uint32_t Book::next_level_idx(std::uint32_t from, Side side) const noexcept {
    // Bids walk downward from the touch, asks upward.
    std::uint32_t const found =
        (side == Side::Buy) ? bid_bits_.highest_le(from) : ask_bits_.lowest_ge(from);
    return (found == TickBitmap::kNone) ? kInvalidIdx : found;
}

Price4 Book::best_bid() const noexcept {
    if (best_bid_idx_ == kInvalidIdx) {
        return Price4{0};
    }
    return price_at_index(best_bid_idx_);
}

Price4 Book::best_ask() const noexcept {
    if (best_ask_idx_ == kInvalidIdx) {
        return Price4{0};
    }
    return price_at_index(best_ask_idx_);
}

bool Book::empty(Side side) const noexcept {
    return side == Side::Buy ? best_bid_idx_ == kInvalidIdx : best_ask_idx_ == kInvalidIdx;
}

}  // namespace lazerbook
