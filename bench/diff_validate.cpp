#include <lazerbook/diff/canonical.hpp>
#include <lazerbook/diff/harness.hpp>
#include <lazerbook/diff/lazerbook_driver.hpp>
#include <lazerbook/diff/liquibook_driver.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

// Random-stress differential test of our Matcher against liquibook. Only built
// when LAZERBOOK_WITH_LIQUIBOOK=ON. Exercises the whole command alphabet --
// limit, market, IOC, FOK, cancel and cancel-replace -- across several seeds.
// A divergence is surfaced with a fill window around it, never papered over.
//
// Usage: diff_validate [--count N] [--seeds N] [--seed HEX]

using namespace lazerbook;
using namespace lazerbook::diff;

namespace {

constexpr std::uint32_t kMinPrice = 100;
constexpr std::uint32_t kNumTicks = 1000;

std::vector<Command> random_commands(std::uint64_t seed, int count) {
    std::vector<Command> cmds;
    cmds.reserve(static_cast<std::size_t>(count));
    std::uint64_t state = seed | 1U;  // xorshift stalls on zero
    auto rng = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    auto price_of = [](std::uint64_t r) {
        return Price4{kMinPrice + static_cast<std::uint32_t>(r % (kNumTicks - 1U))};
    };

    std::uint64_t next_id = 1;
    std::vector<OrderId> live;  // ids currently resting, so cancellable
    auto drop_live = [&live](std::size_t idx) {
        live[idx] = live.back();
        live.pop_back();
    };

    for (int i = 0; i < count; ++i) {
        std::uint64_t const r = rng();
        Command c;
        // Roll the kind out of 100 so the mix is easy to read and adjust.
        std::uint64_t const roll = (r >> 16) % 100U;
        bool const have_live = !live.empty();

        if (roll < 12 && have_live) {
            c.kind = Command::Kind::Cancel;
            std::size_t const idx = static_cast<std::size_t>(r) % live.size();
            c.id = live[idx];
            drop_live(idx);
        } else if (roll < 20 && have_live) {
            // Cancel-replace: old id dies, the replacement id becomes live.
            c.kind = Command::Kind::Modify;
            std::size_t const idx = static_cast<std::size_t>(r) % live.size();
            c.id = live[idx];
            drop_live(idx);
            c.side = (r & 8U) ? Side::Buy : Side::Sell;
            c.modify_new_id = OrderId{next_id++};
            c.modify_new_price = price_of(rng());
            c.modify_new_shares = 1U + static_cast<std::uint32_t>(rng() % 200U);
            live.push_back(c.modify_new_id);
        } else {
            c.id = OrderId{next_id++};
            c.side = (r & 8U) ? Side::Buy : Side::Sell;
            c.price = price_of(r);
            c.shares = 1U + static_cast<std::uint32_t>((r >> 8) % 200U);
            if (roll < 76) {
                c.kind = Command::Kind::NewLimit;
                live.push_back(c.id);
            } else if (roll < 86) {
                c.kind = Command::Kind::NewIoc;
            } else if (roll < 95) {
                c.kind = Command::Kind::NewFok;
            } else {
                c.kind = Command::Kind::NewMarket;
            }
        }
        cmds.push_back(c);
    }
    return cmds;
}

void dump_side(
    char const* label, std::vector<CanonicalFill> const& v, std::size_t lo, std::size_t at
) {
    std::size_t const hi = std::min(v.size(), at + 8);
    std::printf("--- %s fills [%zu..%zu) ---\n", label, lo, hi);
    for (std::size_t i = lo; i < hi; ++i) {
        std::printf(
            "  %s%zu: agg=%llu pass=%llu px=%u sh=%u %s\n", i == at ? ">> " : "   ", i,
            static_cast<unsigned long long>(v[i].aggressor_id),
            static_cast<unsigned long long>(v[i].passive_id), v[i].price_ticks, v[i].shares,
            v[i].aggressor_side == Side::Buy ? "BUY" : "SELL"
        );
    }
}

void dump_window(
    std::vector<CanonicalFill> const& a, std::vector<CanonicalFill> const& b, std::size_t at
) {
    std::size_t const lo = (at > 8) ? at - 8 : 0;
    dump_side("lazerbook", a, lo, at);
    dump_side("liquibook", b, lo, at);
}

int run_one(std::uint64_t seed, int count, bool verbose) {
    auto cmds = random_commands(seed, count);
    LazerbookDriver a(Price4{kMinPrice}, kNumTicks, 1U << 18);
    LiquibookDriver b(Price4{kMinPrice}, kNumTicks, 1U << 18);
    std::vector<CanonicalFill> out_a;
    std::vector<CanonicalFill> out_b;
    DiffResult const r = run_differential(std::span<Command const>(cmds), a, b, out_a, out_b);

    if (r.equal) {
        if (verbose) {
            std::printf(
                "seed=0x%08llX cmds=%d fills=%zu  byte-equal\n",
                static_cast<unsigned long long>(seed), count, r.fills_a
            );
        }
        return 0;
    }
    std::printf(
        "seed=0x%08llX cmds=%d fills_lzb=%zu fills_liq=%zu  DIVERGENCE at fill %zu\n",
        static_cast<unsigned long long>(seed), count, r.fills_a, r.fills_b, r.divergent_at
    );
    dump_window(out_a, out_b, r.divergent_at);
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    int count = 100000;
    int seeds = 8;
    std::uint64_t base_seed = 0xD1FF;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--count") == 0 && i + 1 < argc) {
            count = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--seeds") == 0 && i + 1 < argc) {
            seeds = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            base_seed = std::strtoull(argv[++i], nullptr, 0);
            seeds = 1;
        } else {
            std::fprintf(stderr, "usage: %s [--count N] [--seeds N] [--seed HEX]\n", argv[0]);
            return 2;
        }
    }

    std::printf("# lazerbook diff_validate: %d seeds x %d commands vs liquibook\n", seeds, count);
    int failures = 0;
    for (int s = 0; s < seeds; ++s) {
        // Spread seeds apart so they don't share xorshift trajectory prefixes.
        failures += run_one(
            base_seed + (static_cast<std::uint64_t>(s) * 0x9E3779B97F4A7C15ULL), count, true
        );
    }
    if (failures == 0) {
        std::printf("RESULT: byte-equal across all %d seeds\n", seeds);
        return 0;
    }
    std::printf("RESULT: %d/%d seeds diverged\n", failures, seeds);
    return 1;
}
