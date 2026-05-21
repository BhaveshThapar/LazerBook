#ifndef LAZERBOOK_ITCH_SYNTH_HPP
#define LAZERBOOK_ITCH_SYNTH_HPP

#include <lazerbook/types.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

// Coherent ITCH 5.0 stream generator. Every emitted message respects its
// preconditions (no execute/cancel of an unknown id, no reduce larger than the
// resting size), so a parser + reconstructor fed this stream must reach the same
// aggregate state the synth tracks as ground truth.
namespace lazerbook::itch::synth {

struct Stats {
    std::uint64_t added = 0;
    std::uint64_t executed = 0;
    std::uint64_t cancelled = 0;
    std::uint64_t deleted = 0;
    std::uint64_t replaced = 0;
};

struct GroundTruth {
    std::uint64_t total_bid_shares = 0;
    std::uint64_t total_ask_shares = 0;
    std::uint64_t bid_orders = 0;
    std::uint64_t ask_orders = 0;
    std::uint32_t best_bid = 0;  // 0 if empty
    std::uint32_t best_ask = 0;  // 0 if empty
};

class Synth {
   public:
    Synth(std::uint64_t seed, Price4 min_price, std::uint32_t num_ticks);

    // Writes one message into out; returns bytes written, 0 if out is too small
    // (40 bytes is always sufficient).
    std::size_t next(std::span<std::uint8_t> out);

    [[nodiscard]] Stats const& stats() const noexcept { return stats_; }
    [[nodiscard]] GroundTruth truth() const noexcept;
    [[nodiscard]] std::size_t live_orders() const noexcept { return live_refs_.size(); }

   private:
    struct Live {
        Side side;
        std::uint32_t price_idx;
        std::uint32_t shares;
    };

    std::uint64_t rng();
    std::uint32_t pick_live_index();
    void remove_live(std::uint64_t ref);
    void add_live(std::uint64_t ref, Side side, std::uint32_t price_idx, std::uint32_t shares);

    std::uint64_t state_;
    std::uint32_t lo_;
    std::uint32_t n_;
    std::uint64_t next_ref_ = 1;
    std::uint64_t seq_ = 0;
    std::uint64_t ts_ = 0;

    std::unordered_map<std::uint64_t, Live> live_;
    std::vector<std::uint64_t> live_refs_;  // for uniform random selection
    std::unordered_map<std::uint64_t, std::size_t> ref_pos_;

    std::vector<std::uint64_t> bid_shares_;  // per tick
    std::vector<std::uint64_t> ask_shares_;
    std::uint64_t total_bid_shares_ = 0;
    std::uint64_t total_ask_shares_ = 0;
    std::uint64_t bid_orders_ = 0;
    std::uint64_t ask_orders_ = 0;

    Stats stats_;
};

}  // namespace lazerbook::itch::synth

#endif  // LAZERBOOK_ITCH_SYNTH_HPP
