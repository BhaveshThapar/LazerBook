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

#endif  // LAZERBOOK_WITH_LIQUIBOOK
