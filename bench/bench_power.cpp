#include <lazerbook/bench/clock.hpp>
#include <lazerbook/bench/rapl.hpp>
#include <lazerbook/book.hpp>
#include <lazerbook/events.hpp>
#include <lazerbook/matcher.hpp>
#include <lazerbook/order_pool.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>

using namespace lazerbook;
using namespace lazerbook::bench;

namespace {

struct NullSink final : EventSink {
    void on_fill(Fill const&) override {}
    void on_accepted(OrderAccepted const&) override {}
    void on_cancelled(OrderCancelled const&) override {}
    void on_rejected(OrderRejected const&) override {}
};

}  // namespace

int main() {
    Book book(Price4{100000}, 4000);
    OrderPool pool(1U << 20);
    NullSink sink;
    Matcher m(book, pool, sink);

    EnergyMeasure energy;
    auto const wall_start = std::chrono::steady_clock::now();
    energy.start();

    std::uint64_t matches = 0;
    std::uint64_t id = 1;
    // Insert+match for ~5 seconds of wall time.
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - wall_start).count() <
           5.0) {
        for (int i = 0; i < 100000; ++i) {
            m.on_new(OrderId{id++}, Side::Sell, OrderType::Limit, Price4{101000}, 100);
            m.on_new(OrderId{id++}, Side::Buy, OrderType::Limit, Price4{101000}, 100);
            ++matches;
        }
    }

    energy.stop();
    auto const wall_end = std::chrono::steady_clock::now();
    double const secs = std::chrono::duration<double>(wall_end - wall_start).count();
    double const mops = static_cast<double>(matches) / secs / 1e6;

    std::printf("# lazerbook bench_power\n");
    std::printf(
        "matches=%llu  wall=%.2fs  throughput=%.2f Mops/s\n",
        static_cast<unsigned long long>(matches), secs, mops
    );
    if (energy.available()) {
        double const joules = static_cast<double>(energy.energy_uj()) / 1e6;
        std::printf(
            "energy=%.2f J  joules_per_million_matches=%.4f\n", joules,
            joules / (static_cast<double>(matches) / 1e6)
        );
    } else {
        std::printf("energy=unavailable (RAPL is Linux/x86 only)\n");
    }
    return 0;
}
