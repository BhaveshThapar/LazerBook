#include <lazerbook/diff/canonical.hpp>
#include <lazerbook/diff/harness.hpp>
#include <lazerbook/diff/lazerbook_driver.hpp>
#include <lazerbook/diff/liquibook_driver.hpp>

#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

// Random-stress differential test of our Matcher against liquibook. Only built
// when LAZERBOOK_WITH_LIQUIBOOK=ON. A divergence is surfaced, never hidden: a
// known random-stress divergence around ~50k commands on some seeds is expected
// and reported here rather than papered over.

using namespace lazerbook;
using namespace lazerbook::diff;

namespace {

std::vector<Command> random_commands(std::uint64_t seed, int count) {
    std::vector<Command> cmds;
    cmds.reserve(static_cast<std::size_t>(count));
    std::uint64_t state = seed;
    auto rng = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    std::uint64_t next_id = 1;
    std::vector<OrderId> live;
    for (int i = 0; i < count; ++i) {
        std::uint64_t const r = rng();
        Command c;
        if ((r & 7U) == 0 && !live.empty()) {
            c.kind = Command::Kind::Cancel;
            c.id = live[r % live.size()];
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(r % live.size()));
        } else {
            c.id = OrderId{next_id++};
            c.side = (r & 8U) ? Side::Buy : Side::Sell;
            c.price = Price4{100U + static_cast<std::uint32_t>(r % 800U)};
            c.shares = 1U + static_cast<std::uint32_t>((r >> 8) % 200U);
            std::uint64_t const kind = (r >> 16) % 10U;
            c.kind = (kind < 7) ? Command::Kind::NewLimit
                                : (kind == 7 ? Command::Kind::NewIoc : Command::Kind::NewFok);
            if (c.kind == Command::Kind::NewLimit) {
                live.push_back(c.id);
            }
        }
        cmds.push_back(c);
    }
    return cmds;
}

void dump_window(
    std::vector<CanonicalFill> const& a, std::vector<CanonicalFill> const& b, std::size_t at
) {
    std::size_t const lo = (at > 100) ? at - 100 : 0;
    std::size_t const hi_a = std::min(a.size(), at + 100);
    std::size_t const hi_b = std::min(b.size(), at + 100);
    std::printf("--- lazerbook fills [%zu..%zu) ---\n", lo, hi_a);
    for (std::size_t i = lo; i < hi_a; ++i) {
        std::printf(
            "  %zu: agg=%llu pass=%llu px=%u sh=%u\n", i,
            static_cast<unsigned long long>(a[i].aggressor_id),
            static_cast<unsigned long long>(a[i].passive_id), a[i].price_ticks, a[i].shares
        );
    }
    std::printf("--- liquibook fills [%zu..%zu) ---\n", lo, hi_b);
    for (std::size_t i = lo; i < hi_b; ++i) {
        std::printf(
            "  %zu: agg=%llu pass=%llu px=%u sh=%u\n", i,
            static_cast<unsigned long long>(b[i].aggressor_id),
            static_cast<unsigned long long>(b[i].passive_id), b[i].price_ticks, b[i].shares
        );
    }
}

}  // namespace

int main() {
    auto cmds = random_commands(0xD1FF, 100000);
    LazerbookDriver a(Price4{100}, 1000, 1U << 18);
    LiquibookDriver b(Price4{100}, 1000, 1U << 18);
    std::vector<CanonicalFill> out_a;
    std::vector<CanonicalFill> out_b;
    DiffResult const r = run_differential(std::span<Command const>(cmds), a, b, out_a, out_b);

    std::printf("# lazerbook diff_validate: %zu commands\n", r.commands_processed);
    std::printf("fills_lazerbook=%zu fills_liquibook=%zu\n", r.fills_a, r.fills_b);
    if (r.equal) {
        std::printf("RESULT: byte-equal\n");
        return 0;
    }
    std::printf("RESULT: DIVERGENCE at fill index %zu\n", r.divergent_at);
    dump_window(out_a, out_b, r.divergent_at);
    return 1;
}
