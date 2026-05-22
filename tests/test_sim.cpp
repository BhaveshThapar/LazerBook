#include <lazerbook/sim.hpp>

#include <doctest/doctest.h>
#include <memory>
#include <vector>

using namespace lazerbook;
using namespace lazerbook::sim;

namespace {

std::vector<std::unique_ptr<Agent>> population() {
    std::vector<std::unique_ptr<Agent>> agents;
    agents.push_back(std::make_unique<MarketMaker>(1, 2, 50));
    agents.push_back(std::make_unique<Sniper>(2, 1, 50));
    agents.push_back(std::make_unique<ZeroIntelligence>(3, 0.2, 20));
    return agents;
}

SimConfig config() {
    return SimConfig{Price4{100000}, 400, 1U << 16, 5000, 0xFEEDFACE, Fundamental{100200, 3}};
}

}  // namespace

TEST_CASE("run_cda completes and produces non-trivial fills") {
    SimStats const s = run_cda(config(), population());
    CHECK(s.total_fills > 0);
    CHECK(s.total_shares > 0);
}

TEST_CASE("run_fba completes and produces non-trivial fills") {
    SimStats const s = run_fba(config(), 50, population());
    CHECK(s.total_fills > 0);
    CHECK(s.total_shares > 0);
}

TEST_CASE("a fixed seed yields identical CDA statistics") {
    SimStats const a = run_cda(config(), population());
    SimStats const b = run_cda(config(), population());
    CHECK(a.total_fills == b.total_fills);
    CHECK(a.total_shares == b.total_shares);
    CHECK(a.sniper_pnl == b.sniper_pnl);
    CHECK(a.mm_pnl == b.mm_pnl);
}
