#include <lazerbook/sim.hpp>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace lazerbook;
using namespace lazerbook::sim;

namespace {

std::vector<std::unique_ptr<Agent>> make_population() {
    std::vector<std::unique_ptr<Agent>> a;
    a.push_back(std::make_unique<MarketMaker>(1, 3, 420));
    a.push_back(std::make_unique<Sniper>(2, 1, 420));
    a.push_back(std::make_unique<ZeroIntelligence>(3, 0.3, 150));
    return a;
}

SimConfig config() {
    return SimConfig{Price4{100000}, 1000, 1U << 18, 50000, 0x5A1ED, Fundamental{100500, 5}};
}

void report(char const* mode, SimStats const& s) {
    std::printf(
        "%-12s fills=%-9llu shares=%-11llu mm_pnl=%-14lld mm_inv=%-8lld mm_fills=%-8llu "
        "sniper_pnl=%-14lld sniper_fills=%llu\n",
        mode, static_cast<unsigned long long>(s.total_fills),
        static_cast<unsigned long long>(s.total_shares), static_cast<long long>(s.mm_pnl),
        static_cast<long long>(s.mm_inventory), static_cast<unsigned long long>(s.mm_fills),
        static_cast<long long>(s.sniper_pnl), static_cast<unsigned long long>(s.sniper_fills)
    );
}

}  // namespace

int main() {
    std::printf("# lazerbook fba_compare: identical agents through CDA vs FBA(batch=100)\n");
    report("cda", run_cda(config(), make_population()));
    report("fba(100)", run_fba(config(), 100, make_population()));
    return 0;
}
