#include <lazerbook/itch.hpp>
#include <lazerbook/itch_synth.hpp>

namespace lazerbook::itch::synth {
namespace {

void put_header(std::uint8_t* p, char type, std::uint64_t seq, std::uint64_t ts) {
    p[0] = static_cast<std::uint8_t>(type);
    write_be<std::uint16_t>(p + 1, 1);  // stock_locate
    write_be<std::uint16_t>(p + 3, static_cast<std::uint16_t>(seq & 0xFFFFU));
    write_be48(p + 5, ts & 0xFFFFFFFFFFFFULL);
}

Symbol lzb_symbol() {
    Symbol s;
    constexpr char kName[] = "LZB     ";
    for (std::size_t i = 0; i < s.bytes.size(); ++i) {
        s.bytes[i] = kName[i];
    }
    return s;
}

}  // namespace

Synth::Synth(std::uint64_t seed, Price4 min_price, std::uint32_t num_ticks)
    : state_(seed == 0 ? 0x9E3779B97F4A7C15ULL : seed),
      lo_(value_of(min_price)),
      n_(num_ticks),
      bid_shares_(num_ticks, 0),
      ask_shares_(num_ticks, 0) {}

std::uint64_t Synth::rng() {
    // splitmix64
    std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

void Synth::add_live(std::uint64_t ref, Side side, std::uint32_t price_idx, std::uint32_t shares) {
    live_.emplace(ref, Live{side, price_idx, shares});
    ref_pos_.emplace(ref, live_refs_.size());
    live_refs_.push_back(ref);
    if (side == Side::Buy) {
        bid_shares_[price_idx] += shares;
        total_bid_shares_ += shares;
        ++bid_orders_;
    } else {
        ask_shares_[price_idx] += shares;
        total_ask_shares_ += shares;
        ++ask_orders_;
    }
}

void Synth::remove_live(std::uint64_t ref) {
    auto it = live_.find(ref);
    Live const& l = it->second;
    if (l.side == Side::Buy) {
        bid_shares_[l.price_idx] -= l.shares;
        total_bid_shares_ -= l.shares;
        --bid_orders_;
    } else {
        ask_shares_[l.price_idx] -= l.shares;
        total_ask_shares_ -= l.shares;
        --ask_orders_;
    }
    // swap-and-pop from live_refs_
    std::size_t const pos = ref_pos_[ref];
    std::uint64_t const last = live_refs_.back();
    live_refs_[pos] = last;
    ref_pos_[last] = pos;
    live_refs_.pop_back();
    ref_pos_.erase(ref);
    live_.erase(it);
}

std::uint32_t Synth::pick_live_index() {
    return static_cast<std::uint32_t>(rng() % live_refs_.size());
}

std::size_t Synth::next(std::span<std::uint8_t> out) {
    if (out.size() < 40) {
        return 0;
    }
    std::uint8_t* p = out.data();
    ++seq_;
    ts_ += 1 + (rng() % 64);

    std::size_t const live = live_refs_.size();
    std::uint32_t const roll = static_cast<std::uint32_t>(rng() % 100);
    std::uint32_t const add_weight = (live < 16) ? 70 : 35;

    if (live == 0 || roll < add_weight) {
        // --- A AddOrder ---
        std::uint64_t const ref = next_ref_++;
        Side const side = (rng() & 1U) ? Side::Buy : Side::Sell;
        std::uint32_t const idx = static_cast<std::uint32_t>(rng() % n_);
        std::uint32_t const shares = 1 + static_cast<std::uint32_t>(rng() % 1000);
        put_header(p, 'A', seq_, ts_);
        write_be<std::uint64_t>(p + 11, ref);
        p[19] = static_cast<std::uint8_t>(static_cast<char>(side));
        write_be<std::uint32_t>(p + 20, shares);
        write_symbol(p + 24, lzb_symbol());
        write_be<Price4>(p + 32, Price4{lo_ + idx});
        add_live(ref, side, idx, shares);
        ++stats_.added;
        return 36;
    }

    std::uint32_t const action = static_cast<std::uint32_t>(rng() % 4);
    std::uint64_t const ref = live_refs_[pick_live_index()];
    Live const l = live_.at(ref);

    if (action == 0) {
        // --- E OrderExecuted (partial or full) ---
        std::uint32_t const exec = 1 + static_cast<std::uint32_t>(rng() % l.shares);
        put_header(p, 'E', seq_, ts_);
        write_be<std::uint64_t>(p + 11, ref);
        write_be<std::uint32_t>(p + 19, exec);
        write_be<std::uint64_t>(p + 23, seq_);  // match number
        if (exec >= l.shares) {
            remove_live(ref);
        } else {
            Live& m = live_.at(ref);
            m.shares -= exec;
            if (l.side == Side::Buy) {
                bid_shares_[l.price_idx] -= exec;
                total_bid_shares_ -= exec;
            } else {
                ask_shares_[l.price_idx] -= exec;
                total_ask_shares_ -= exec;
            }
        }
        ++stats_.executed;
        return 31;
    }
    if (action == 1) {
        // --- X OrderCancel (partial reduce) ---
        std::uint32_t const cancel = 1 + static_cast<std::uint32_t>(rng() % l.shares);
        put_header(p, 'X', seq_, ts_);
        write_be<std::uint64_t>(p + 11, ref);
        write_be<std::uint32_t>(p + 19, cancel);
        if (cancel >= l.shares) {
            remove_live(ref);
        } else {
            Live& m = live_.at(ref);
            m.shares -= cancel;
            if (l.side == Side::Buy) {
                bid_shares_[l.price_idx] -= cancel;
                total_bid_shares_ -= cancel;
            } else {
                ask_shares_[l.price_idx] -= cancel;
                total_ask_shares_ -= cancel;
            }
        }
        ++stats_.cancelled;
        return 23;
    }
    if (action == 2) {
        // --- D OrderDelete (full) ---
        put_header(p, 'D', seq_, ts_);
        write_be<std::uint64_t>(p + 11, ref);
        remove_live(ref);
        ++stats_.deleted;
        return 19;
    }

    // --- U OrderReplace (same side, new ref/price/shares) ---
    std::uint64_t const new_ref = next_ref_++;
    std::uint32_t const new_idx = static_cast<std::uint32_t>(rng() % n_);
    std::uint32_t const new_shares = 1 + static_cast<std::uint32_t>(rng() % 1000);
    put_header(p, 'U', seq_, ts_);
    write_be<std::uint64_t>(p + 11, ref);
    write_be<std::uint64_t>(p + 19, new_ref);
    write_be<std::uint32_t>(p + 27, new_shares);
    write_be<Price4>(p + 31, Price4{lo_ + new_idx});
    Side const side = l.side;
    remove_live(ref);
    add_live(new_ref, side, new_idx, new_shares);
    ++stats_.replaced;
    return 35;
}

GroundTruth Synth::truth() const noexcept {
    GroundTruth g;
    g.total_bid_shares = total_bid_shares_;
    g.total_ask_shares = total_ask_shares_;
    g.bid_orders = bid_orders_;
    g.ask_orders = ask_orders_;
    g.best_bid = 0;
    for (std::uint32_t i = n_; i-- > 0;) {
        if (bid_shares_[i] > 0) {
            g.best_bid = lo_ + i;
            break;
        }
    }
    g.best_ask = 0;
    for (std::uint32_t i = 0; i < n_; ++i) {
        if (ask_shares_[i] > 0) {
            g.best_ask = lo_ + i;
            break;
        }
    }
    return g;
}

}  // namespace lazerbook::itch::synth
