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

   private:
    bool is_buy_;
    liquibook::book::Price price_;
    liquibook::book::Quantity qty_;
    std::uint64_t id_;
    bool ioc_;
    bool aon_;
};

using LbOrderPtr = std::shared_ptr<LbOrder>;

class LiquibookDriver {
   public:
    LiquibookDriver(Price4 /*min_price*/, std::uint32_t /*num_ticks*/, std::size_t /*pool*/) {}

    void operator()(std::span<Command const> cmds, std::vector<CanonicalFill>& out) {
        Listener listener(out);
        liquibook::book::OrderBook<LbOrderPtr> book;
        book.set_order_listener(&listener);
        std::unordered_map<std::uint64_t, LbOrderPtr> live;

        for (Command const& c : cmds) {
            dispatch(book, live, listener, c);
        }
    }

   private:
    // Maps liquibook fill callbacks onto CanonicalFill. liquibook reports a fill
    // from the perspective of the inbound (aggressor) order.
    class Listener : public liquibook::book::OrderListener<LbOrderPtr> {
       public:
        explicit Listener(std::vector<CanonicalFill>& out) : out_(out) {}

        void on_fill(
            LbOrderPtr const& inbound, LbOrderPtr const& matched, liquibook::book::Quantity qty,
            liquibook::book::Price price
        ) {
            out_.push_back(CanonicalFill{
                inbound->id(), matched->id(), static_cast<std::uint32_t>(price),
                static_cast<std::uint32_t>(qty), inbound->is_buy() ? Side::Buy : Side::Sell
            });
        }
        void on_accept(LbOrderPtr const&) {}
        void on_reject(LbOrderPtr const&, char const*) {}
        void on_cancel(LbOrderPtr const&) {}
        void on_cancel_reject(LbOrderPtr const&, char const*) {}
        void on_replace(LbOrderPtr const&, int64_t, liquibook::book::Price) {}
        void on_replace_reject(LbOrderPtr const&, char const*) {}

       private:
        std::vector<CanonicalFill>& out_;
    };

    static void dispatch(
        liquibook::book::OrderBook<LbOrderPtr>& book,
        std::unordered_map<std::uint64_t, LbOrderPtr>& live, Listener& /*l*/, Command const& c
    ) {
        auto make = [&](bool ioc, bool aon) {
            auto o = std::make_shared<LbOrder>(
                c.side == Side::Buy, value_of(c.price), c.shares, value_of(c.id), ioc, aon
            );
            live[value_of(c.id)] = o;
            book.add(o);
            book.perform_callbacks();
        };
        switch (c.kind) {
            case Command::Kind::NewLimit:
                make(false, false);
                break;
            case Command::Kind::NewMarket:
                make(false, false);
                break;  // 0-price market proxy
            case Command::Kind::NewIoc:
                make(true, false);
                break;
            case Command::Kind::NewFok:
                make(true, true);
                break;
            case Command::Kind::Cancel: {
                auto it = live.find(value_of(c.id));
                if (it != live.end()) {
                    book.cancel(it->second);
                    book.perform_callbacks();
                }
                break;
            }
            case Command::Kind::Modify: {
                // Our semantics are cancel-replace, so emulate that rather than
                // liquibook's in-place replace (which keeps time priority).
                auto it = live.find(value_of(c.id));
                if (it != live.end()) {
                    book.cancel(it->second);
                    book.perform_callbacks();
                    live.erase(it);
                }
                auto o = std::make_shared<LbOrder>(
                    c.side == Side::Buy, value_of(c.modify_new_price), c.modify_new_shares,
                    value_of(c.modify_new_id), false, false
                );
                live[value_of(c.modify_new_id)] = o;
                book.add(o);
                book.perform_callbacks();
                break;
            }
        }
    }
};

}  // namespace lazerbook::diff

#endif  // LAZERBOOK_WITH_LIQUIBOOK
#endif  // LAZERBOOK_DIFF_LIQUIBOOK_DRIVER_HPP
