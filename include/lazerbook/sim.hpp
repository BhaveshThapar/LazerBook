#ifndef LAZERBOOK_SIM_HPP
#define LAZERBOOK_SIM_HPP

#include <lazerbook/events.hpp>
#include <lazerbook/matcher.hpp>
#include <lazerbook/types.hpp>

#include <cstdint>
#include <memory>
#include <vector>

// Tiny multi-agent market simulator. A scalar "fundamental" follows a random
// walk; agents quote / snipe / trade around it through a CDA or FBA matcher.
// Used to reproduce the Budish-Cramton-Shim result that frequent batch auctions
// destroy the latency-sniping rent that a continuous market hands out.
namespace lazerbook::sim {

enum class AgentKind : std::uint8_t { MarketMaker, Sniper, ZeroIntelligence };

// Deterministic splitmix64 PRNG shared across the run.
class Rng {
   public:
    explicit Rng(std::uint64_t seed) : state_(seed == 0 ? 0xDEADBEEFULL : seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    // Approximately-normal increment in [-bound, bound] via summed uniforms.
    std::int32_t walk(std::uint32_t bound) {
        if (bound == 0) {
            return 0;
        }
        std::int64_t acc = 0;
        for (int i = 0; i < 4; ++i) {
            acc += static_cast<std::int64_t>(next() % (2U * bound + 1U)) - bound;
        }
        return static_cast<std::int32_t>(acc / 4);
    }

   private:
    std::uint64_t state_;
};

struct MarketView {
    Price4 best_bid{0};
    Price4 best_ask{0};
    std::uint32_t tick{0};
};

struct Command {
    enum class Kind : std::uint8_t { New, Cancel };
    Kind kind{Kind::New};
    OrderId id{};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    Price4 price{};
    std::uint32_t shares{};
};

class Actions {
   public:
    void submit(OrderId id, Side side, OrderType type, Price4 price, std::uint32_t shares) {
        cmds_.push_back(Command{Command::Kind::New, id, side, type, price, shares});
    }
    void cancel(OrderId id) {
        cmds_.push_back(Command{Command::Kind::Cancel, id, Side::Buy, OrderType::Limit, Price4{}, 0}
        );
    }
    [[nodiscard]] std::vector<Command> const& commands() const noexcept { return cmds_; }
    void clear() noexcept { cmds_.clear(); }

   private:
    std::vector<Command> cmds_;
};

// Agent base. Order ids are namespaced by prefix() in the top byte so the sink
// can attribute fills back to the originating agent.
class Agent {
   public:
    explicit Agent(std::uint8_t prefix) : prefix_(prefix) {}
    Agent(Agent const&) = delete;
    Agent& operator=(Agent const&) = delete;
    Agent(Agent&&) = delete;
    Agent& operator=(Agent&&) = delete;
    virtual ~Agent() = default;

    virtual void on_tick(MarketView const&, Price4 fundamental, Actions&, Rng&) = 0;
    [[nodiscard]] virtual AgentKind kind() const noexcept = 0;

    [[nodiscard]] std::uint8_t prefix() const noexcept { return prefix_; }
    void record_fill(Side bought_or_sold, Price4 price, std::uint32_t shares) noexcept;

    [[nodiscard]] std::int64_t cash() const noexcept { return cash_; }
    [[nodiscard]] std::int64_t inventory() const noexcept { return inventory_; }
    [[nodiscard]] std::uint64_t fills() const noexcept { return fills_; }
    [[nodiscard]] std::int64_t pnl(Price4 mark) const noexcept {
        return cash_ + inventory_ * static_cast<std::int64_t>(value_of(mark));
    }

   protected:
    [[nodiscard]] OrderId mint() noexcept {
        return OrderId{(static_cast<std::uint64_t>(prefix_) << 56) | (++counter_)};
    }
    // Hook so a market maker can re-quote a side that just got hit.
    virtual void notify_fill(Side) noexcept {}

   private:
    std::uint8_t prefix_;
    std::uint64_t counter_ = 0;
    std::int64_t cash_ = 0;
    std::int64_t inventory_ = 0;
    std::uint64_t fills_ = 0;
};

class MarketMaker final : public Agent {
   public:
    MarketMaker(std::uint8_t prefix, std::uint32_t half_spread_ticks, std::uint32_t qty)
        : Agent(prefix), half_spread_(half_spread_ticks), qty_(qty) {}
    void on_tick(MarketView const&, Price4 fundamental, Actions&, Rng&) override;
    [[nodiscard]] AgentKind kind() const noexcept override { return AgentKind::MarketMaker; }

   protected:
    void notify_fill(Side side) noexcept override {
        if (side == Side::Buy) {
            has_bid_ = false;
        } else {
            has_ask_ = false;
        }
    }

   private:
    std::uint32_t half_spread_;
    std::uint32_t qty_;
    bool has_bid_ = false;
    bool has_ask_ = false;
    OrderId bid_id_{};
    OrderId ask_id_{};
    std::uint32_t bid_px_ = 0;
    std::uint32_t ask_px_ = 0;
};

class Sniper final : public Agent {
   public:
    Sniper(std::uint8_t prefix, std::uint32_t edge_ticks, std::uint32_t qty)
        : Agent(prefix), edge_(edge_ticks), qty_(qty) {}
    void on_tick(MarketView const&, Price4 fundamental, Actions&, Rng&) override;
    [[nodiscard]] AgentKind kind() const noexcept override { return AgentKind::Sniper; }

   private:
    std::uint32_t edge_;
    std::uint32_t qty_;
};

class ZeroIntelligence final : public Agent {
   public:
    ZeroIntelligence(std::uint8_t prefix, double rate, std::uint32_t qty)
        : Agent(prefix), rate_(rate), qty_(qty) {}
    void on_tick(MarketView const&, Price4 fundamental, Actions&, Rng&) override;
    [[nodiscard]] AgentKind kind() const noexcept override { return AgentKind::ZeroIntelligence; }

   private:
    double rate_;
    std::uint32_t qty_;
};

struct Fundamental {
    std::uint32_t value;
    std::uint32_t sigma;
};

struct SimConfig {
    Price4 min_price;
    std::uint32_t num_ticks;
    std::size_t pool_size;
    std::uint32_t total_ticks;
    std::uint64_t seed;
    Fundamental fundamental;
};

struct SimStats {
    std::uint64_t total_fills = 0;
    std::uint64_t total_shares = 0;
    std::int64_t mm_pnl = 0;
    std::int64_t mm_inventory = 0;
    std::uint64_t mm_fills = 0;
    std::int64_t sniper_pnl = 0;
    std::uint64_t sniper_fills = 0;
};

SimStats run_cda(SimConfig const& cfg, std::vector<std::unique_ptr<Agent>> agents);
SimStats run_fba(
    SimConfig const& cfg, std::uint64_t batch_ticks, std::vector<std::unique_ptr<Agent>> agents
);

}  // namespace lazerbook::sim

#endif  // LAZERBOOK_SIM_HPP
