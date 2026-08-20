// Differential tests against the vendored liquibook reference engine. These only
// compile and run when built with -DLAZERBOOK_WITH_LIQUIBOOK=ON and the submodule
// is present; otherwise this translation unit is intentionally empty.
#if defined(LAZERBOOK_WITH_LIQUIBOOK)

#include <lazerbook/diff/canonical.hpp>
#include <lazerbook/diff/harness.hpp>
#include <lazerbook/diff/lazerbook_driver.hpp>
#include <lazerbook/diff/liquibook_driver.hpp>

#include <doctest/doctest.h>
#include <span>
#include <vector>

using namespace lazerbook;
using namespace lazerbook::diff;

namespace {

DiffResult compare(std::vector<Command> const& cmds) {
    LazerbookDriver a(Price4{100}, 1000, 4096);
    LiquibookDriver b(Price4{100}, 1000, 4096);
    std::vector<CanonicalFill> oa;
    std::vector<CanonicalFill> ob;
    return run_differential(std::span<Command const>(cmds), a, b, oa, ob);
}

Command lim(OrderId id, Side s, std::uint32_t px, std::uint32_t sh) {
    return Command{Command::Kind::NewLimit, id, s, Price4{px}, sh, {}, {}, {}};
}

Command aggressive(Command::Kind k, OrderId id, Side s, std::uint32_t px, std::uint32_t sh) {
    return Command{k, id, s, Price4{px}, sh, {}, {}, {}};
}

Command cancel(OrderId id) {
    return Command{Command::Kind::Cancel, id, Side::Buy, Price4{0}, 0, {}, {}, {}};
}

Command modify(OrderId old_id, OrderId new_id, std::uint32_t px, std::uint32_t sh) {
    return Command{Command::Kind::Modify, old_id, Side::Buy, Price4{0}, 0, new_id, Price4{px}, sh};
}

}  // namespace

TEST_CASE("liquibook diff: simple cross") {
    std::vector<Command> cmds{
        lim(OrderId{1}, Side::Sell, 150, 10), lim(OrderId{2}, Side::Buy, 150, 10)
    };
    CHECK(compare(cmds).equal);
}

TEST_CASE("liquibook diff: level walk") {
    std::vector<Command> cmds{
        lim(OrderId{1}, Side::Sell, 150, 5), lim(OrderId{2}, Side::Sell, 151, 5),
        lim(OrderId{3}, Side::Sell, 152, 5), lim(OrderId{4}, Side::Buy, 152, 12)
    };
    CHECK(compare(cmds).equal);
}

TEST_CASE("liquibook diff: time priority within a level") {
    std::vector<Command> cmds{
        lim(OrderId{1}, Side::Sell, 150, 5), lim(OrderId{2}, Side::Sell, 150, 5),
        lim(OrderId{3}, Side::Buy, 150, 7)
    };
    CHECK(compare(cmds).equal);
}

TEST_CASE("liquibook diff: cancel then trade") {
    std::vector<Command> cmds{
        lim(OrderId{1}, Side::Sell, 150, 5), lim(OrderId{2}, Side::Sell, 151, 5),
        Command{Command::Kind::Cancel, OrderId{1}, Side::Sell, Price4{150}, 0, {}, {}, {}},
        lim(OrderId{3}, Side::Buy, 151, 5)
    };
    CHECK(compare(cmds).equal);
}

// --- Regression cases for the semantics that used to diverge ----------------
// Each of these was byte-unequal before the liquibook driver learned to pass
// OrderConditions explicitly and to track which orders are actually resting.

TEST_CASE("liquibook diff: unfillable FOK does not rest") {
    // The original divergence: a FOK with no crossing liquidity must be killed.
    // liquibook only honours that when oc_fill_or_kill is passed to add().
    std::vector<Command> cmds{
        lim(OrderId{1}, Side::Buy, 465, 88),
        aggressive(Command::Kind::NewFok, OrderId{2}, Side::Sell, 471, 118),
        // If the FOK wrongly rested at 471, this buy would fill against it.
        lim(OrderId{3}, Side::Buy, 851, 82),
    };
    CHECK(compare(cmds).equal);
}

TEST_CASE("liquibook diff: partially fillable FOK does not rest") {
    std::vector<Command> cmds{
        lim(OrderId{1}, Side::Buy, 500, 50),
        aggressive(Command::Kind::NewFok, OrderId{2}, Side::Sell, 500, 120),
        lim(OrderId{3}, Side::Buy, 900, 200),
    };
    CHECK(compare(cmds).equal);
}

TEST_CASE("liquibook diff: IOC residual does not rest") {
    std::vector<Command> cmds{
        lim(OrderId{1}, Side::Buy, 500, 30),
        aggressive(Command::Kind::NewIoc, OrderId{2}, Side::Sell, 500, 100),
        lim(OrderId{3}, Side::Buy, 900, 200),
    };
    CHECK(compare(cmds).equal);
}

TEST_CASE("liquibook diff: market order sweeps and never rests") {
    std::vector<Command> cmds{
        lim(OrderId{1}, Side::Sell, 300, 10),
        lim(OrderId{2}, Side::Sell, 400, 10),
        aggressive(Command::Kind::NewMarket, OrderId{3}, Side::Buy, 0, 50),
        lim(OrderId{4}, Side::Sell, 900, 40),
    };
    CHECK(compare(cmds).equal);
}

TEST_CASE("liquibook diff: modify is cancel-replace and loses time priority") {
    std::vector<Command> cmds{
        lim(OrderId{1}, Side::Buy, 500, 10),
        lim(OrderId{2}, Side::Buy, 500, 10),
        modify(OrderId{1}, OrderId{3}, 500, 10),
        // Order 1 moved behind order 2, so this sell must hit 2 first.
        lim(OrderId{4}, Side::Sell, 500, 15),
    };
    CHECK(compare(cmds).equal);
}

TEST_CASE("liquibook diff: modify of a fully filled order creates nothing") {
    // Order 1 is consumed, so it is no longer resting. Matcher::on_modify
    // rejects and creates no replacement; the reference must agree.
    std::vector<Command> cmds{
        lim(OrderId{1}, Side::Buy, 500, 10),
        lim(OrderId{2}, Side::Sell, 500, 10),  // fully consumes order 1
        modify(OrderId{1}, OrderId{3}, 600, 50),
        // If a phantom order 3 rested at 600, this sell would fill against it.
        lim(OrderId{4}, Side::Sell, 550, 50),
    };
    CHECK(compare(cmds).equal);
}

TEST_CASE("liquibook diff: modify of an unknown id creates nothing") {
    std::vector<Command> cmds{
        lim(OrderId{1}, Side::Buy, 500, 10),
        modify(OrderId{99}, OrderId{3}, 600, 50),
        lim(OrderId{4}, Side::Sell, 550, 50),
    };
    CHECK(compare(cmds).equal);
}

TEST_CASE("liquibook diff: cancel of a fully filled order is a no-op") {
    std::vector<Command> cmds{
        lim(OrderId{1}, Side::Buy, 500, 10),
        lim(OrderId{2}, Side::Sell, 500, 10),
        cancel(OrderId{1}),
        lim(OrderId{3}, Side::Sell, 500, 10),
    };
    CHECK(compare(cmds).equal);
}

TEST_CASE("liquibook diff: randomized stress over the full command alphabet") {
    // Small in-CI mirror of bench/diff_validate. Exercises limit, IOC, FOK,
    // market, cancel and cancel-replace against the reference engine.
    constexpr std::uint32_t kMinPrice = 100;
    constexpr std::uint32_t kNumTicks = 1000;
    std::uint64_t state = 0xC0FFEE;
    auto rng = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };

    std::vector<Command> cmds;
    std::uint64_t next_id = 1;
    std::vector<OrderId> live;
    for (int i = 0; i < 20000; ++i) {
        std::uint64_t const r = rng();
        std::uint64_t const roll = (r >> 16) % 100U;
        Command c;
        if (roll < 12 && !live.empty()) {
            std::size_t const idx = static_cast<std::size_t>(r) % live.size();
            c = cancel(live[idx]);
            live[idx] = live.back();
            live.pop_back();
        } else if (roll < 20 && !live.empty()) {
            std::size_t const idx = static_cast<std::size_t>(r) % live.size();
            OrderId const nid{next_id++};
            c = modify(
                live[idx], nid, kMinPrice + static_cast<std::uint32_t>(rng() % (kNumTicks - 1U)),
                1U + static_cast<std::uint32_t>(rng() % 200U)
            );
            live[idx] = live.back();
            live.pop_back();
            live.push_back(nid);
        } else {
            OrderId const id{next_id++};
            Side const side = (r & 8U) ? Side::Buy : Side::Sell;
            auto const px = kMinPrice + static_cast<std::uint32_t>(r % (kNumTicks - 1U));
            auto const sh = 1U + static_cast<std::uint32_t>((r >> 8) % 200U);
            if (roll < 76) {
                c = lim(id, side, px, sh);
                live.push_back(id);
            } else if (roll < 86) {
                c = aggressive(Command::Kind::NewIoc, id, side, px, sh);
            } else if (roll < 95) {
                c = aggressive(Command::Kind::NewFok, id, side, px, sh);
            } else {
                c = aggressive(Command::Kind::NewMarket, id, side, px, sh);
            }
        }
        cmds.push_back(c);
    }

    LazerbookDriver a(Price4{kMinPrice}, kNumTicks, 1U << 16);
    LiquibookDriver b(Price4{kMinPrice}, kNumTicks, 1U << 16);
    std::vector<CanonicalFill> oa;
    std::vector<CanonicalFill> ob;
    DiffResult const r = run_differential(std::span<Command const>(cmds), a, b, oa, ob);
    CHECK(r.fills_a > 5000);  // guard against a vacuously equal empty run
    CHECK(r.fills_a == r.fills_b);
    CHECK(r.equal);
}

#endif  // LAZERBOOK_WITH_LIQUIBOOK
