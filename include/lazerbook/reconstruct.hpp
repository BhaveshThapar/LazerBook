#ifndef LAZERBOOK_RECONSTRUCT_HPP
#define LAZERBOOK_RECONSTRUCT_HPP

#include <lazerbook/book.hpp>
#include <lazerbook/itch.hpp>
#include <lazerbook/itch_view.hpp>
#include <lazerbook/order_index.hpp>
#include <lazerbook/order_pool.hpp>
#include <lazerbook/types.hpp>

#include <cstdint>
#include <span>

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

    // Zero-copy path: decodes straight off the wire bytes without
    // materialising the owning variant. Returns the message length consumed,
    // or 0 if the buffer is short or the type is unrecognised. Semantics are
    // identical to apply_bytes -- test_reconstruct.cpp asserts they agree.
    std::size_t apply_view_bytes(std::span<std::uint8_t const> bytes);

    [[nodiscard]] Stats const& stats() const noexcept { return stats_; }
    [[nodiscard]] std::size_t live_order_count() const noexcept { return orders_.size(); }

    // See Matcher::prefetch.
    void prefetch(OrderId ref) const noexcept { orders_.prefetch(ref); }

   private:
    void add_order(OrderId ref, Side side, Price4 price, std::uint32_t shares);
    void reduce_order(OrderId ref, std::uint32_t qty);
    void delete_order(OrderId ref);
    void replace_order(OrderId original, OrderId replacement, Price4 price, std::uint32_t shares);

    // Adapts itch::visit's view dispatch onto the same mutations apply() uses,
    // so the two entry points cannot drift apart.
    struct ViewHandler {
        Reconstructor& r;

        void on_message(itch::AddOrderView const& v) {
            r.add_order(v.order_reference_number(), v.buy_sell_indicator(), v.price(), v.shares());
            ++r.stats_.added;
        }
        void on_message(itch::OrderExecutedView const& v) {
            r.reduce_order(v.order_reference_number(), v.executed_shares());
            ++r.stats_.executed;
        }
        void on_message(itch::OrderCancelView const& v) {
            r.reduce_order(v.order_reference_number(), v.cancelled_shares());
            ++r.stats_.cancelled;
        }
        void on_message(itch::OrderDeleteView const& v) {
            r.delete_order(v.order_reference_number());
            ++r.stats_.deleted;
        }
        void on_message(itch::OrderReplaceView const& v) {
            r.replace_order(
                v.original_order_reference_number(), v.new_order_reference_number(), v.price(),
                v.shares()
            );
        }
        void on_message(itch::TradeView const&) { ++r.stats_.trades; }
        void on_message(itch::CrossTradeView const&) { ++r.stats_.trades; }
        void on_message(itch::BrokenTradeView const&) { ++r.stats_.trades; }
        // Parsed but not applied to the book.
        void on_message(itch::SystemEventView const&) { ++r.stats_.skip_unhandled; }
        void on_message(itch::StockDirectoryView const&) { ++r.stats_.skip_unhandled; }
        void on_message(itch::StockTradingActionView const&) { ++r.stats_.skip_unhandled; }
        void on_message(itch::RegShoView const&) { ++r.stats_.skip_unhandled; }
        void on_message(itch::NoiiView const&) { ++r.stats_.skip_unhandled; }
        void on_unhandled(itch::MessageView const&) { ++r.stats_.skip_unhandled; }
    };

    Book& book_;
    OrderPool& pool_;
    OrderIndex orders_;
    Stats stats_;
};

}  // namespace lazerbook

#endif  // LAZERBOOK_RECONSTRUCT_HPP
