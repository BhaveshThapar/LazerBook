#ifndef LAZERBOOK_MATCHER_HPP
#define LAZERBOOK_MATCHER_HPP

#include <lazerbook/book.hpp>
#include <lazerbook/events.hpp>
#include <lazerbook/order_index.hpp>
#include <lazerbook/order_pool.hpp>
#include <lazerbook/types.hpp>

#include <cstdint>

namespace lazerbook {

enum class OrderType : std::uint8_t { Limit, Market, Ioc, Fok };

// Continuous Double Auction matcher with strict price-time priority. Aggressors
// walk the opposite side from the best price; each crossing resting order yields
// a Fill at the passive's price. Nothing on the insert / cancel / match path
// allocates: orders come from the pool and the id index is preallocated.
class Matcher {
   public:
    Matcher(Book& book, OrderPool& pool, EventSink& sink);

    void on_new(OrderId id, Side side, OrderType type, Price4 price, std::uint32_t shares);
    void on_cancel(OrderId id);
    // Cancel-replace: drops old_id, inserts new_id at new price/shares.
    void on_modify(OrderId old_id, OrderId new_id, Price4 new_price, std::uint32_t new_shares);

    [[nodiscard]] std::size_t resting_count() const noexcept { return resting_.size(); }

    // Warm the id index for the order about to arrive. The framed stream is
    // sequential, so a caller can issue this several messages ahead and hide
    // the miss on an index too large for L3.
    void prefetch(OrderId id) const noexcept { resting_.prefetch(id); }

   private:
    // Returns remaining aggressor shares after consuming crossing liquidity.
    std::uint32_t match(OrderId id, Side side, Price4 limit, std::uint32_t shares, bool unbounded);
    [[nodiscard]] bool crosses(Side aggressor, Price4 limit, Price4 resting_px, bool unbounded)
        const noexcept;
    [[nodiscard]] bool can_fully_fill(Side side, Price4 limit, std::uint32_t shares) const noexcept;
    void rest(OrderId id, Side side, Price4 price, std::uint32_t shares);

    Book& book_;
    OrderPool& pool_;
    EventSink& sink_;
    // Sized from the pool: there can never be more resting orders than slots,
    // and 2x keeps the load factor at or below 0.5 for short probe chains.
    OrderIndex resting_;
};

}  // namespace lazerbook

#endif  // LAZERBOOK_MATCHER_HPP
