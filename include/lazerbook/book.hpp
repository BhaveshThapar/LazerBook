#ifndef LAZERBOOK_BOOK_HPP
#define LAZERBOOK_BOOK_HPP

#include <lazerbook/order.hpp>
#include <lazerbook/order_pool.hpp>
#include <lazerbook/tick_bitmap.hpp>
#include <lazerbook/types.hpp>

#include <cstdint>
#include <vector>

namespace lazerbook {

// One price tick on one side: an intrusive FIFO of resting orders plus
// O(1) aggregate counters.
struct PriceLevel {
    Order* head = nullptr;
    Order* tail = nullptr;
    std::uint64_t total_shares = 0;
    std::uint32_t order_count = 0;

    [[nodiscard]] bool empty() const noexcept { return head == nullptr; }
};

// Single-symbol, dense price-indexed order book. Prices in [min_price,
// min_price + num_ticks) map directly to array slots, so best-bid/ask lookups
// are cursor reads and level access is a bounds check + index. Time priority
// within a level is FIFO (append at tail). The book stores liquidity only; it
// performs no matching.
class Book {
   public:
    Book(Price4 min_price, std::uint32_t num_ticks);

    void add(Order* o) noexcept;                       // append at its level's tail
    void remove(Order* o) noexcept;                    // unlink; caller releases to pool
    void reduce(Order* o, std::uint32_t by) noexcept;  // dec shares, unlink at 0

    [[nodiscard]] Price4 best_bid() const noexcept;  // 0 if empty
    [[nodiscard]] Price4 best_ask() const noexcept;  // 0 if empty
    [[nodiscard]] bool empty(Side side) const noexcept;

    [[nodiscard]] PriceLevel* level_at(Price4 price, Side side) noexcept;
    [[nodiscard]] PriceLevel const* level_at(Price4 price, Side side) const noexcept;

    [[nodiscard]] bool in_range(Price4 price) const noexcept;
    // Precondition: in_range(price).
    [[nodiscard]] std::uint32_t index_of_price(Price4 price) const noexcept {
        return value_of(price) - value_of(min_price_);
    }
    [[nodiscard]] Price4 min_price() const noexcept { return min_price_; }
    [[nodiscard]] std::uint32_t num_ticks() const noexcept { return num_ticks_; }

    // First non-empty level at or beyond `from`, walking away from the touch.
    // O(1) via the occupancy bitmap; used by FOK's fillable-quantity check so
    // it visits only populated levels instead of every tick in range.
    [[nodiscard]] std::uint32_t next_level_idx(std::uint32_t from, Side side) const noexcept;
    [[nodiscard]] PriceLevel const& level_by_index(std::uint32_t idx, Side side) const noexcept {
        return (side == Side::Buy) ? bids_[idx] : asks_[idx];
    }
    [[nodiscard]] Price4 price_at_index(std::uint32_t idx) const noexcept {
        return Price4{value_of(min_price_) + idx};
    }

    static constexpr std::uint32_t kInvalidIdx = 0xFFFFFFFFU;

   private:
    [[nodiscard]] std::uint32_t index_of(Price4 price) const noexcept;

    Price4 min_price_;
    std::uint32_t num_ticks_;
    std::vector<PriceLevel> bids_;  // indexed by price - min_price
    std::vector<PriceLevel> asks_;
    // Occupancy summaries. Emptying the touch used to trigger a linear rescan
    // over every tick; these turn it into a countl_zero / countr_zero.
    TickBitmap bid_bits_;
    TickBitmap ask_bits_;
    // best_bid_idx_: highest non-empty bid index. best_ask_idx_: lowest
    // non-empty ask index. kInvalidIdx means that side is empty. Cached rather
    // than queried each time so the match loop stays a register read.
    std::uint32_t best_bid_idx_;
    std::uint32_t best_ask_idx_;
};

}  // namespace lazerbook

#endif  // LAZERBOOK_BOOK_HPP
