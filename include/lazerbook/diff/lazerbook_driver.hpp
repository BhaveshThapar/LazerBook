#ifndef LAZERBOOK_DIFF_LAZERBOOK_DRIVER_HPP
#define LAZERBOOK_DIFF_LAZERBOOK_DRIVER_HPP

#include <lazerbook/book.hpp>
#include <lazerbook/diff/canonical.hpp>
#include <lazerbook/events.hpp>
#include <lazerbook/matcher.hpp>
#include <lazerbook/order_pool.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace lazerbook::diff {

// Drives our own Matcher and captures its fills as CanonicalFills.
class LazerbookDriver {
   public:
    LazerbookDriver(Price4 min_price, std::uint32_t num_ticks, std::size_t pool_size)
        : min_price_(min_price), num_ticks_(num_ticks), pool_size_(pool_size) {}

    void operator()(std::span<Command const> cmds, std::vector<CanonicalFill>& out) {
        Book book(min_price_, num_ticks_);
        OrderPool pool(pool_size_);
        CapturingSink sink(out);
        Matcher m(book, pool, sink);
        for (Command const& c : cmds) {
            dispatch(m, c);
        }
    }

   private:
    class CapturingSink final : public EventSink {
       public:
        explicit CapturingSink(std::vector<CanonicalFill>& out) : out_(out) {}
        void on_fill(Fill const& f) override {
            out_.push_back(CanonicalFill{
                value_of(f.aggressor_id), value_of(f.passive_id), value_of(f.price), f.shares,
                f.aggressor_side
            });
        }
        void on_accepted(OrderAccepted const&) override {}
        void on_cancelled(OrderCancelled const&) override {}
        void on_rejected(OrderRejected const&) override {}

       private:
        std::vector<CanonicalFill>& out_;
    };

    static void dispatch(Matcher& m, Command const& c) {
        switch (c.kind) {
            case Command::Kind::NewLimit:
                m.on_new(c.id, c.side, OrderType::Limit, c.price, c.shares);
                break;
            case Command::Kind::NewMarket:
                m.on_new(c.id, c.side, OrderType::Market, c.price, c.shares);
                break;
            case Command::Kind::NewIoc:
                m.on_new(c.id, c.side, OrderType::Ioc, c.price, c.shares);
                break;
            case Command::Kind::NewFok:
                m.on_new(c.id, c.side, OrderType::Fok, c.price, c.shares);
                break;
            case Command::Kind::Cancel:
                m.on_cancel(c.id);
                break;
            case Command::Kind::Modify:
                m.on_modify(c.id, c.modify_new_id, c.modify_new_price, c.modify_new_shares);
                break;
        }
    }

    Price4 min_price_;
    std::uint32_t num_ticks_;
    std::size_t pool_size_;
};

}  // namespace lazerbook::diff

#endif  // LAZERBOOK_DIFF_LAZERBOOK_DRIVER_HPP
