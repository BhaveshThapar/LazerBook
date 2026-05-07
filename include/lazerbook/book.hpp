#ifndef LAZERBOOK_BOOK_HPP
#define LAZERBOOK_BOOK_HPP

#include <lazerbook/order.hpp>
#include <lazerbook/order_pool.hpp>
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
    [[nodiscard]] Price4 min_price() const noexcept { return min_price_; }
    [[nodiscard]] std::uint32_t num_ticks() const noexcept { return num_ticks_; }

   private:
    [[nodiscard]] std::uint32_t index_of(Price4 price) const noexcept;
    void refresh_best_bid_down(std::uint32_t from_idx) noexcept;
    void refresh_best_ask_up(std::uint32_t from_idx) noexcept;

    Price4 min_price_;
    std::uint32_t num_ticks_;
    std::vector<PriceLevel> bids_;  // indexed by price - min_price
    std::vector<PriceLevel> asks_;
    // best_bid_idx_: highest non-empty bid index. best_ask_idx_: lowest
    // non-empty ask index. kInvalidIdx means that side is empty.
    std::uint32_t best_bid_idx_;
    std::uint32_t best_ask_idx_;

    static constexpr std::uint32_t kInvalidIdx = 0xFFFFFFFFU;
};

}  // namespace lazerbook

#endif  // LAZERBOOK_BOOK_HPP
