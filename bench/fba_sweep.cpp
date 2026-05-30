#include <lazerbook/sim.hpp>

#include <array>
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

SimConfig config(std::uint64_t seed) {
    return SimConfig{Price4{100000}, 1000, 1U << 18, 50000, seed, Fundamental{100500, 5}};
}

constexpr std::array<std::uint64_t, 11> kBatches{0, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000};
constexpr std::array<std::uint64_t, 3> kSeeds{0xA11CE, 0xB0B, 0xC0FFEE};

}  // namespace

int main() {
    std::printf("mode,batch_ticks,sniper_pnl_mean,mm_pnl_mean,fills_mean\n");
    for (std::uint64_t batch : kBatches) {
        double sniper = 0;
        double mm = 0;
        double fills = 0;
        for (std::uint64_t seed : kSeeds) {
            SimStats const s = (batch == 0) ? run_cda(config(seed), make_population())
                                            : run_fba(config(seed), batch, make_population());
            sniper += static_cast<double>(s.sniper_pnl);
            mm += static_cast<double>(s.mm_pnl);
            fills += static_cast<double>(s.total_fills);
        }
        double const n = static_cast<double>(kSeeds.size());
        char const* mode = (batch == 0) ? "cda" : "fba";
        std::printf(
            "%s,%llu,%.1f,%.1f,%.1f\n", mode, static_cast<unsigned long long>(batch), sniper / n,
            mm / n, fills / n
        );
    }
    return 0;
}
