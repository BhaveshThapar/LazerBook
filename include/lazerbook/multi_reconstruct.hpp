#ifndef LAZERBOOK_MULTI_RECONSTRUCT_HPP
#define LAZERBOOK_MULTI_RECONSTRUCT_HPP

#include <lazerbook/book_registry.hpp>
#include <lazerbook/itch_view.hpp>
#include <lazerbook/order_index.hpp>
#include <lazerbook/order_pool.hpp>
#include <lazerbook/types.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace lazerbook {

// Reconstructs every instrument in an ITCH stream at once.
//
// The single-symbol Reconstructor decodes Symbol and stock_locate and then
// discards both, routing every message to one book -- so pointing it at a real
// capture would merge ~9000 instruments into a single price ladder. This
// routes on the stock_locate that every ITCH header carries.
//
// Order reference numbers are unique across the whole day, not per instrument,
// so one index maps ref -> Order. The instrument an order belongs to is
// recorded alongside it, because E/C/X/D/U name only the reference and the
// book that owns the order has to be recoverable from it.
class MultiReconstructor {
   public:
    struct Stats {
        std::uint64_t added = 0;
        std::uint64_t executed = 0;
        std::uint64_t cancelled = 0;
        std::uint64_t deleted = 0;
        std::uint64_t replaced = 0;
        std::uint64_t trades = 0;
        std::uint64_t directory = 0;  // 'R' messages seen
        std::uint64_t skip_unknown_ref = 0;
        std::uint64_t skip_oor = 0;  // price outside the instrument's window
        std::uint64_t skip_pool_exhausted = 0;
        std::uint64_t skip_no_book = 0;  // locate outside the configured range
        std::uint64_t skip_unhandled = 0;
        std::uint64_t skip_parse_error = 0;
    };

    MultiReconstructor(std::uint16_t max_locate, std::size_t pool_size)
        : MultiReconstructor(max_locate, pool_size, BookRegistry::Config{}) {}

    MultiReconstructor(std::uint16_t max_locate, std::size_t pool_size, BookRegistry::Config cfg)
        : registry_(max_locate, cfg),
          pool_(pool_size),
          orders_(pool_size * 2),
          owner_(pool_size, 0) {}

    // Applies one framed message. Returns bytes consumed, 0 if undecodable.
    std::size_t apply(std::span<std::uint8_t const> bytes) {
        Handler h{*this};
        std::size_t const consumed = itch::visit(bytes, h);
        if (consumed == 0) {
            ++stats_.skip_parse_error;
        }
        return consumed;
    }

    [[nodiscard]] Stats const& stats() const noexcept { return stats_; }
    [[nodiscard]] BookRegistry const& registry() const noexcept { return registry_; }
    [[nodiscard]] BookRegistry& registry() noexcept { return registry_; }
    [[nodiscard]] std::size_t live_order_count() const noexcept { return orders_.size(); }
    [[nodiscard]] std::size_t pool_in_use() const noexcept { return pool_.in_use(); }

    // Aggregate resting shares on one side across every instrument.
    [[nodiscard]] std::uint64_t total_shares(Side side) const {
        std::uint64_t total = 0;
        for (std::size_t i = 0; i < registry_.capacity(); ++i) {
            Book const* b = registry_.find(static_cast<std::uint16_t>(i));
            if (b == nullptr) {
                continue;
            }
            for (std::uint32_t t = 0; t < b->num_ticks(); ++t) {
                total += b->level_by_index(t, side).total_shares;
            }
        }
        return total;
    }

   private:
    // Slot index of an Order within the pool's storage, used to associate an
    // order with the instrument that owns it without growing Order itself.
    [[nodiscard]] std::size_t slot_of(Order const* o) const noexcept { return pool_.index_of(o); }

    void add_order(
        std::uint16_t locate, OrderId ref, Side side, Price4 price, std::uint32_t shares
    ) {
        Book* book = registry_.get_or_create(locate, price);
        if (book == nullptr) {
            ++stats_.skip_no_book;
            return;
        }
        if (!book->in_range(price)) {
            ++stats_.skip_oor;
            return;
        }
        Order* o = pool_.acquire();
        if (o == nullptr) {
            ++stats_.skip_pool_exhausted;
            return;
        }
        o->id = ref;
        o->side = side;
        o->price = price;
        o->shares = shares;
        book->add(o);
        orders_.insert(ref, o);
        owner_[slot_of(o)] = locate;
    }

    void reduce_order(OrderId ref, std::uint32_t qty) {
        Order* o = orders_.find(ref);
        if (o == nullptr) {
            ++stats_.skip_unknown_ref;
            return;
        }
        Book* book = registry_.find(owner_[slot_of(o)]);
        if (book == nullptr) {
            ++stats_.skip_no_book;
            return;
        }
        if (qty >= o->shares) {
            book->reduce(o, o->shares);  // unlinks at zero
            pool_.release(o);
            orders_.erase(ref);
        } else {
            book->reduce(o, qty);
        }
    }

    void delete_order(OrderId ref) {
        Order* o = orders_.find(ref);
        if (o == nullptr) {
            ++stats_.skip_unknown_ref;
            return;
        }
        Book* book = registry_.find(owner_[slot_of(o)]);
        if (book == nullptr) {
            ++stats_.skip_no_book;
            return;
        }
        book->remove(o);
        pool_.release(o);
        orders_.erase(ref);
    }

    void replace_order(OrderId original, OrderId replacement, Price4 price, std::uint32_t shares) {
        Order* prev = orders_.find(original);
        if (prev == nullptr) {
            ++stats_.skip_unknown_ref;
            ++stats_.replaced;
            return;
        }
        std::uint16_t const locate = owner_[slot_of(prev)];
        Side const side = prev->side;
        Book* book = registry_.find(locate);
        if (book != nullptr) {
            book->remove(prev);
        }
        pool_.release(prev);
        orders_.erase(original);
        add_order(locate, replacement, side, price, shares);
        ++stats_.replaced;
    }

    struct Handler {
        MultiReconstructor& r;

        void on_message(itch::AddOrderView const& v) {
            r.add_order(
                v.stock_locate(), v.order_reference_number(), v.buy_sell_indicator(), v.price(),
                v.shares()
            );
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
        void on_message(itch::StockDirectoryView const&) { ++r.stats_.directory; }
        void on_message(itch::SystemEventView const&) { ++r.stats_.skip_unhandled; }
        void on_message(itch::StockTradingActionView const&) { ++r.stats_.skip_unhandled; }
        void on_message(itch::RegShoView const&) { ++r.stats_.skip_unhandled; }
        void on_message(itch::NoiiView const&) { ++r.stats_.skip_unhandled; }
        void on_unhandled(itch::MessageView const&) { ++r.stats_.skip_unhandled; }
    };

    BookRegistry registry_;
    OrderPool pool_;
    OrderIndex orders_;
    // Instrument owning each pool slot. One byte pair per slot rather than a
    // field on Order, which is deliberately kept at 40 bytes.
    std::vector<std::uint16_t> owner_;
    Stats stats_;
};

}  // namespace lazerbook

#endif  // LAZERBOOK_MULTI_RECONSTRUCT_HPP
