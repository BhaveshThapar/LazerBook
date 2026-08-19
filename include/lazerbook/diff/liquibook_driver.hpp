#ifndef LAZERBOOK_DIFF_LIQUIBOOK_DRIVER_HPP
#define LAZERBOOK_DIFF_LIQUIBOOK_DRIVER_HPP

#include <lazerbook/diff/canonical.hpp>

// This driver wraps the vendored liquibook reference engine. It is only usable
// when the third_party/liquibook submodule is present and the project is built
// with -DLAZERBOOK_WITH_LIQUIBOOK=ON; otherwise the whole adapter compiles away.
#if defined(LAZERBOOK_WITH_LIQUIBOOK)

#include <book/order_book.h>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace lazerbook::diff {

// Minimal liquibook order satisfying its order concept.
class LbOrder {
   public:
    LbOrder(
        bool is_buy, liquibook::book::Price price, liquibook::book::Quantity qty, std::uint64_t id,
        bool ioc, bool aon
    )
        : is_buy_(is_buy), price_(price), qty_(qty), id_(id), ioc_(ioc), aon_(aon) {}

    [[nodiscard]] bool is_buy() const { return is_buy_; }
    [[nodiscard]] liquibook::book::Price price() const { return price_; }
    [[nodiscard]] liquibook::book::Quantity order_qty() const { return qty_; }
    [[nodiscard]] std::uint64_t id() const { return id_; }
    [[nodiscard]] bool immediate_or_cancel() const { return ioc_; }
    [[nodiscard]] bool all_or_none() const { return aon_; }
    // Required by OrderBook::add / ::cancel. We model no stop orders, and
    // liquibook treats 0 as "not a stop order".
    [[nodiscard]] liquibook::book::Price stop_price() const { return 0; }
    [[nodiscard]] bool is_limit() const { return price_ > 0; }

    // Fill bookkeeping so the driver can tell which orders are still resting.
    void add_filled(liquibook::book::Quantity q) { filled_ += q; }
    [[nodiscard]] bool fully_filled() const { return filled_ >= qty_; }

   private:
    bool is_buy_;
    liquibook::book::Price price_;
    liquibook::book::Quantity qty_;
    std::uint64_t id_;
    bool ioc_;
    bool aon_;
    liquibook::book::Quantity filled_{0};
};

using LbOrderPtr = std::shared_ptr<LbOrder>;

class LiquibookDriver {
   public:
    LiquibookDriver(Price4 /*min_price*/, std::uint32_t /*num_ticks*/, std::size_t /*pool*/) {}

    void operator()(std::span<Command const> cmds, std::vector<CanonicalFill>& out) {
        std::unordered_map<std::uint64_t, LbOrderPtr> live;
        Listener listener(out, live);
        liquibook::book::OrderBook<LbOrderPtr> book;
        book.set_order_listener(&listener);

        for (Command const& c : cmds) {
            dispatch(book, live, listener, c);
        }
    }

   private:
    // Maps liquibook fill callbacks onto CanonicalFill. liquibook reports a fill
    // from the perspective of the inbound (aggressor) order.
    class Listener : public liquibook::book::OrderListener<LbOrderPtr> {
       public:
        Listener(
            std::vector<CanonicalFill>& out, std::unordered_map<std::uint64_t, LbOrderPtr>& live
        )
            : out_(out), live_(live) {}
        // liquibook's OrderListener has no virtual dtor; declare one here so
        // -Wnon-virtual-dtor is satisfied without patching the vendored header.
        virtual ~Listener() = default;
        Listener(Listener const&) = delete;
        Listener& operator=(Listener const&) = delete;
        Listener(Listener&&) = delete;
        Listener& operator=(Listener&&) = delete;

        void on_fill(
            LbOrderPtr const& inbound, LbOrderPtr const& matched, liquibook::book::Quantity qty,
            liquibook::book::Price price
        ) override {
            out_.push_back(CanonicalFill{
                inbound->id(), matched->id(), static_cast<std::uint32_t>(price),
                static_cast<std::uint32_t>(qty), inbound->is_buy() ? Side::Buy : Side::Sell
            });
            // Mirror Matcher::resting_: an order that is fully filled no longer
            // rests, so a later cancel/modify against it must find nothing.
            inbound->add_filled(qty);
            matched->add_filled(qty);
            if (inbound->fully_filled()) {
                live_.erase(inbound->id());
            }
            if (matched->fully_filled()) {
                live_.erase(matched->id());
            }
        }
        void on_accept(LbOrderPtr const&) override {}
        void on_reject(LbOrderPtr const& o, char const*) override { live_.erase(o->id()); }
        void on_cancel(LbOrderPtr const& o) override { live_.erase(o->id()); }
        void on_cancel_reject(LbOrderPtr const&, char const*) override {}
        // Note the reference: liquibook declares size_delta as `const int64_t&`.
        // Taking it by value silently fails to override and leaves this abstract.
        void on_replace(LbOrderPtr const&, std::int64_t const&, liquibook::book::Price) override {}
        void on_replace_reject(LbOrderPtr const&, char const*) override {}

       private:
        std::vector<CanonicalFill>& out_;
        std::unordered_map<std::uint64_t, LbOrderPtr>& live_;
    };

    static void dispatch(
        liquibook::book::OrderBook<LbOrderPtr>& book,
        std::unordered_map<std::uint64_t, LbOrderPtr>& live, Listener& /*l*/, Command const& c
    ) {
        namespace lb = liquibook::book;

        // liquibook only honours IOC / AON when the condition mask is passed to
        // add(). Its OrderTracker ctor does read order->immediate_or_cancel(),
        // but that block is behind LIQUIBOOK_ORDER_KNOWS_CONDITIONS *and* ORs
        // into the by-value parameter after conditions_(conditions) has already
        // run in the init list, so it is dead either way. Passing the mask
        // explicitly is the only route that works.
        auto make = [&](lb::Price px, std::uint32_t qty, std::uint64_t id, bool is_buy,
                        lb::OrderConditions cond) {
            bool const ioc = (cond & lb::oc_immediate_or_cancel) != 0;
            bool const aon = (cond & lb::oc_all_or_none) != 0;
            auto o = std::make_shared<LbOrder>(is_buy, px, qty, id, ioc, aon);
            // Only resting orders are cancellable later; IOC/FOK never rest.
            if (!ioc) {
                live[id] = o;
            }
            book.add(o, cond);
        };

        bool const is_buy = (c.side == Side::Buy);
        switch (c.kind) {
            case Command::Kind::NewLimit:
                make(value_of(c.price), c.shares, value_of(c.id), is_buy, lb::oc_no_conditions);
                break;
            case Command::Kind::NewMarket:
                // liquibook treats price 0 as a market order (Order::is_limit()
                // is price() > 0). Ours never rests, so mark it IOC too.
                make(0, c.shares, value_of(c.id), is_buy, lb::oc_immediate_or_cancel);
                break;
            case Command::Kind::NewIoc:
                make(
                    value_of(c.price), c.shares, value_of(c.id), is_buy, lb::oc_immediate_or_cancel
                );
                break;
            case Command::Kind::NewFok:
                make(value_of(c.price), c.shares, value_of(c.id), is_buy, lb::oc_fill_or_kill);
                break;
            // Note: book.cancel() fires on_cancel synchronously, and the listener
            // erases from `live` -- so hold the OrderPtr by value and erase by
            // key. Erasing through an iterator here is a use-after-invalidation.
            case Command::Kind::Cancel: {
                auto it = live.find(value_of(c.id));
                if (it != live.end()) {
                    LbOrderPtr const o = it->second;
                    book.cancel(o);
                    live.erase(value_of(c.id));
                }
                break;
            }
            case Command::Kind::Modify: {
                // Our semantics are cancel-replace, so emulate that rather than
                // liquibook's in-place replace (which keeps time priority).
                // Matcher::on_modify reuses the *original* order's side, so the
                // replacement must inherit it here too -- c.side is not it.
                auto it = live.find(value_of(c.id));
                if (it == live.end()) {
                    break;  // not resting: both engines reject, creating nothing
                }
                LbOrderPtr const o = it->second;
                bool const replace_is_buy = o->is_buy();
                book.cancel(o);
                live.erase(value_of(c.id));
                make(
                    value_of(c.modify_new_price), c.modify_new_shares, value_of(c.modify_new_id),
                    replace_is_buy, lb::oc_no_conditions
                );
                break;
            }
        }
    }
};

}  // namespace lazerbook::diff

#endif  // LAZERBOOK_WITH_LIQUIBOOK
#endif  // LAZERBOOK_DIFF_LIQUIBOOK_DRIVER_HPP
