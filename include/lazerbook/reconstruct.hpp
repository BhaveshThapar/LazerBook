#ifndef LAZERBOOK_RECONSTRUCT_HPP
#define LAZERBOOK_RECONSTRUCT_HPP

#include <lazerbook/book.hpp>
#include <lazerbook/itch.hpp>
#include <lazerbook/order_pool.hpp>
#include <lazerbook/types.hpp>

#include <cstdint>
#include <span>
#include <unordered_map>

namespace lazerbook {

// Applies parsed ITCH messages to a Book. This is NOT a matcher: ITCH reports
// what already happened on the exchange, so reconstruct just mutates the book to
// mirror it. Bad references / out-of-range prices / pool exhaustion are counted,
// never fatal.
class Reconstructor {
   public:
    struct Stats {
        std::uint64_t added = 0;
        std::uint64_t executed = 0;
        std::uint64_t cancelled = 0;
        std::uint64_t deleted = 0;
        std::uint64_t replaced = 0;
        std::uint64_t trades = 0;  // P / Q / B
        std::uint64_t skip_unknown_ref = 0;
        std::uint64_t skip_oor = 0;
        std::uint64_t skip_pool_exhausted = 0;
        std::uint64_t skip_unhandled = 0;
        std::uint64_t skip_parse_error = 0;
    };

    Reconstructor(Book& book, OrderPool& pool);

    void apply(itch::Message const& msg);
    void apply_bytes(std::span<std::uint8_t const> bytes);

    [[nodiscard]] Stats const& stats() const noexcept { return stats_; }
    [[nodiscard]] std::size_t live_order_count() const noexcept { return orders_.size(); }

   private:
    void add_order(OrderId ref, Side side, Price4 price, std::uint32_t shares);
    void reduce_order(OrderId ref, std::uint32_t qty);
    void delete_order(OrderId ref);

    Book& book_;
    OrderPool& pool_;
    std::unordered_map<std::uint64_t, Order*> orders_;
    Stats stats_;
};

}  // namespace lazerbook

#endif  // LAZERBOOK_RECONSTRUCT_HPP
