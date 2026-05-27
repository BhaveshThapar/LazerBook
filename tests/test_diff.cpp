#include <lazerbook/diff/canonical.hpp>
#include <lazerbook/diff/harness.hpp>
#include <lazerbook/diff/lazerbook_driver.hpp>

#include <doctest/doctest.h>
#include <span>
#include <vector>

using namespace lazerbook;
using namespace lazerbook::diff;

namespace {

std::vector<Command> crossing_sequence() {
    std::vector<Command> cmds;
    cmds.push_back(
        Command{Command::Kind::NewLimit, OrderId{1}, Side::Sell, Price4{150}, 5, {}, {}, {}}
    );
    cmds.push_back(
        Command{Command::Kind::NewLimit, OrderId{2}, Side::Sell, Price4{151}, 5, {}, {}, {}}
    );
    cmds.push_back(
        Command{Command::Kind::NewLimit, OrderId{3}, Side::Buy, Price4{151}, 8, {}, {}, {}}
    );
    return cmds;
}

}  // namespace

TEST_CASE("identical drivers report equal fill streams") {
    auto cmds = crossing_sequence();
    LazerbookDriver a(Price4{100}, 100, 256);
    LazerbookDriver b(Price4{100}, 100, 256);
    std::vector<CanonicalFill> out_a;
    std::vector<CanonicalFill> out_b;
    DiffResult const r = run_differential(std::span<Command const>(cmds), a, b, out_a, out_b);
    CHECK(r.equal);
    CHECK(r.fills_a == r.fills_b);
    CHECK(r.fills_a >= 2);
}

TEST_CASE("the harness flags an injected divergence at the right index") {
    auto cmds = crossing_sequence();
    LazerbookDriver a(Price4{100}, 100, 256);
    // Driver B runs the real engine, then corrupts the shares of the 2nd fill.
    auto b = [](std::span<Command const> c, std::vector<CanonicalFill>& out) {
        LazerbookDriver inner(Price4{100}, 100, 256);
        inner(c, out);
        if (out.size() >= 2) {
            out[1].shares += 1;
        }
    };
    std::vector<CanonicalFill> out_a;
    std::vector<CanonicalFill> out_b;
    DiffResult const r = run_differential(std::span<Command const>(cmds), a, b, out_a, out_b);
    CHECK_FALSE(r.equal);
    CHECK(r.divergent_at == 1);
}

TEST_CASE("the harness flags a fill-count mismatch") {
    auto cmds = crossing_sequence();
    LazerbookDriver a(Price4{100}, 100, 256);
    auto b = [](std::span<Command const> c, std::vector<CanonicalFill>& out) {
        LazerbookDriver inner(Price4{100}, 100, 256);
        inner(c, out);
        out.pop_back();  // drop a fill
    };
    std::vector<CanonicalFill> out_a;
    std::vector<CanonicalFill> out_b;
    DiffResult const r = run_differential(std::span<Command const>(cmds), a, b, out_a, out_b);
    CHECK_FALSE(r.equal);
    CHECK(r.fills_a != r.fills_b);
}
