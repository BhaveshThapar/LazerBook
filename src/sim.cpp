#include <lazerbook/book.hpp>
#include <lazerbook/fba.hpp>
#include <lazerbook/order_pool.hpp>
#include <lazerbook/sim.hpp>

#include <algorithm>
#include <array>

namespace lazerbook::sim {
namespace {

std::uint32_t clamp_px(std::int64_t px, std::uint32_t lo, std::uint32_t hi) {
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(px, lo, hi));
}

// Attributes fills back to agents by the order-id prefix byte.
class SimSink final : public EventSink {
   public:
    explicit SimSink(std::vector<std::unique_ptr<Agent>> const& agents) {
        for (auto const& a : agents) {
            by_prefix_[a->prefix()] = a.get();
        }
    }

    void on_fill(Fill const& f) override {
        ++total_fills;
        total_shares += f.shares;
        attribute(f.aggressor_id, f.aggressor_side, f.price, f.shares);
        Side const passive = (f.aggressor_side == Side::Buy) ? Side::Sell : Side::Buy;
        attribute(f.passive_id, passive, f.price, f.shares);
    }
    void on_accepted(OrderAccepted const&) override {}
    void on_cancelled(OrderCancelled const&) override {}
    void on_rejected(OrderRejected const&) override {}

    std::uint64_t total_fills = 0;
    std::uint64_t total_shares = 0;

   private:
    void attribute(OrderId id, Side side, Price4 px, std::uint32_t shares) {
        auto const prefix = static_cast<std::uint8_t>(value_of(id) >> 56);
        if (Agent* a = by_prefix_[prefix]) {
            a->record_fill(side, px, shares);
        }
    }

    std::array<Agent*, 256> by_prefix_{};
};

template <typename Apply>
SimStats run_loop(
    SimConfig const& cfg, std::vector<std::unique_ptr<Agent>>& agents, SimSink& sink,
    Apply&& apply_commands
) {
    Rng rng(cfg.seed);
    std::uint32_t const lo = value_of(cfg.min_price);
    std::uint32_t const hi = lo + cfg.num_ticks - 1;
    std::uint32_t const margin = std::max<std::uint32_t>(1, cfg.num_ticks / 4);
    std::int64_t fund = cfg.fundamental.value;

    Actions acts;
    for (std::uint32_t tick = 0; tick < cfg.total_ticks; ++tick) {
        fund += rng.walk(cfg.fundamental.sigma);
        fund = clamp_px(fund, lo + margin, hi - margin);
        apply_commands(tick, Price4{static_cast<std::uint32_t>(fund)}, acts, rng);
    }

    SimStats stats;
    stats.total_fills = sink.total_fills;
    stats.total_shares = sink.total_shares;
    Price4 const mark{static_cast<std::uint32_t>(fund)};
    for (auto const& a : agents) {
        if (a->kind() == AgentKind::MarketMaker) {
            stats.mm_pnl += a->pnl(mark);
            stats.mm_inventory += a->inventory();
            stats.mm_fills += a->fills();
        } else if (a->kind() == AgentKind::Sniper) {
            stats.sniper_pnl += a->pnl(mark);
            stats.sniper_fills += a->fills();
        }
    }
    return stats;
}

}  // namespace

void Agent::record_fill(Side bought_or_sold, Price4 price, std::uint32_t shares) noexcept {
    std::int64_t const notional =
        static_cast<std::int64_t>(value_of(price)) * static_cast<std::int64_t>(shares);
    if (bought_or_sold == Side::Buy) {
        cash_ -= notional;
        inventory_ += shares;
    } else {
        cash_ += notional;
        inventory_ -= shares;
    }
    ++fills_;
    notify_fill(bought_or_sold);
}

void MarketMaker::on_tick(MarketView const&, Price4 fundamental, Actions& acts, Rng&) {
    std::uint32_t const f = value_of(fundamental);
    std::uint32_t const desired_bid = f - half_spread_;
    std::uint32_t const desired_ask = f + half_spread_;
    if (!has_bid_ || bid_px_ != desired_bid) {
        if (has_bid_) {
            acts.cancel(bid_id_);
        }
        bid_id_ = mint();
        acts.submit(bid_id_, Side::Buy, OrderType::Limit, Price4{desired_bid}, qty_);
        bid_px_ = desired_bid;
        has_bid_ = true;
    }
    if (!has_ask_ || ask_px_ != desired_ask) {
        if (has_ask_) {
            acts.cancel(ask_id_);
        }
        ask_id_ = mint();
        acts.submit(ask_id_, Side::Sell, OrderType::Limit, Price4{desired_ask}, qty_);
        ask_px_ = desired_ask;
        has_ask_ = true;
    }
}

void Sniper::on_tick(MarketView const& view, Price4 fundamental, Actions& acts, Rng&) {
    std::uint32_t const f = value_of(fundamental);
    if (value_of(view.best_ask) != 0 && f > value_of(view.best_ask) + edge_) {
        acts.submit(mint(), Side::Buy, OrderType::Ioc, view.best_ask, qty_);
    } else if (value_of(view.best_bid) != 0 && value_of(view.best_bid) > f + edge_) {
        acts.submit(mint(), Side::Sell, OrderType::Ioc, view.best_bid, qty_);
    }
}

void ZeroIntelligence::on_tick(MarketView const&, Price4 fundamental, Actions& acts, Rng& rng) {
    if (static_cast<double>(rng.next() % 1000000U) / 1.0e6 >= rate_) {
        return;
    }
    Side const side = (rng.next() & 1U) ? Side::Buy : Side::Sell;
    std::int64_t const off = static_cast<std::int64_t>(rng.next() % 11U) - 5;
    auto const px =
        static_cast<std::uint32_t>(static_cast<std::int64_t>(value_of(fundamental)) + off);
    acts.submit(mint(), side, OrderType::Limit, Price4{px}, qty_);
}

SimStats run_cda(SimConfig const& cfg, std::vector<std::unique_ptr<Agent>> agents) {
    Book book(cfg.min_price, cfg.num_ticks);
    OrderPool pool(cfg.pool_size);
    SimSink sink(agents);
    Matcher matcher(book, pool, sink);

    auto apply = [&](std::uint32_t tick, Price4 fundamental, Actions& acts, Rng& rng) {
        MarketView const view{book.best_bid(), book.best_ask(), tick};
        acts.clear();
        for (auto const& a : agents) {
            a->on_tick(view, fundamental, acts, rng);
        }
        for (Command const& c : acts.commands()) {
            if (c.kind == Command::Kind::New) {
                matcher.on_new(c.id, c.side, c.type, c.price, c.shares);
            } else {
                matcher.on_cancel(c.id);
            }
        }
    };
    return run_loop(cfg, agents, sink, apply);
}

SimStats run_fba(
    SimConfig const& cfg, std::uint64_t batch_ticks, std::vector<std::unique_ptr<Agent>> agents
) {
    Book book(cfg.min_price, cfg.num_ticks);
    OrderPool pool(cfg.pool_size);
    SimSink sink(agents);
    FbaMatcher matcher(book, pool, sink, cfg.seed);
    std::uint64_t const batch = (batch_ticks == 0) ? 1 : batch_ticks;

    auto apply = [&](std::uint32_t tick, Price4 fundamental, Actions& acts, Rng& rng) {
        MarketView const view{book.best_bid(), book.best_ask(), tick};
        acts.clear();
        for (auto const& a : agents) {
            a->on_tick(view, fundamental, acts, rng);
        }
        for (Command const& c : acts.commands()) {
            if (c.kind == Command::Kind::New) {
                matcher.on_new(c.id, c.side, c.price, c.shares);
            } else {
                matcher.on_cancel(c.id);
            }
        }
        if ((static_cast<std::uint64_t>(tick) + 1) % batch == 0) {
            matcher.clear();
        }
    };
    SimStats s = run_loop(cfg, agents, sink, apply);
    matcher.clear();  // final partial batch
    s.total_fills = sink.total_fills;
    s.total_shares = sink.total_shares;
    return s;
}

}  // namespace lazerbook::sim
