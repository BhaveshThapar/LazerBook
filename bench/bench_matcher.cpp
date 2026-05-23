#include <lazerbook/bench/clock.hpp>
#include <lazerbook/bench/percentile.hpp>
#include <lazerbook/book.hpp>
#include <lazerbook/events.hpp>
#include <lazerbook/matcher.hpp>
#include <lazerbook/order_pool.hpp>

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

constexpr int kIters = 100000;

void print(char const* name, Report const& r) {
    std::printf(
        "%-26s n=%-8zu p50=%8.1f p90=%8.1f p99=%8.1f p99.9=%8.1f max=%9.1f mean=%7.1f ns\n", name,
        r.n, r.p50, r.p90, r.p99, r.p999, r.max, r.mean
    );
}

// Scenario 1: limit insert that never matches (the canonical hot path).
void bench_insert_no_match() {
    Book book(Price4{100000}, 4000);
    OrderPool pool(kIters + 16);
    NullSink sink;
    Matcher m(book, pool, sink);
    Percentiles p;
    p.reserve(kIters);
    for (int i = 0; i < kIters; ++i) {
        auto const id = OrderId{static_cast<std::uint64_t>(i) + 1};
        auto const px = Price4{100000U + static_cast<std::uint32_t>(i % 2000)};
        std::uint64_t const t0 = now_ticks();
        m.on_new(id, Side::Buy, OrderType::Limit, px, 100);
        std::uint64_t const t1 = now_ticks();
        p.add(ticks_to_ns(t1 - t0));
    }
    print("limit-insert-no-match", p.report());
}

// Scenario 2: every insert crosses resting liquidity and produces one fill,
// against a *liquid* book (depth that does not empty), as a real name behaves.
// Each timed buy partially consumes the top ask, so the best-ask cursor never
// has to rescan an emptied side -- the cost is the pure insert+match path.
void bench_insert_one_fill() {
    Book book(Price4{100000}, 4000);
    OrderPool pool(1024);
    NullSink sink;
    Matcher m(book, pool, sink);
    // Seed a deep, persistent ask: a top level large enough to back every timed
    // buy, plus depth behind it so the level structure is realistic.
    auto const top_qty = static_cast<std::uint32_t>(kIters) * 100U + 1000U;
    m.on_new(OrderId{1}, Side::Sell, OrderType::Limit, Price4{101000}, top_qty);
    for (int k = 1; k <= 8; ++k) {
        m.on_new(
            OrderId{static_cast<std::uint64_t>(k) + 1}, Side::Sell, OrderType::Limit,
            Price4{101000U + static_cast<std::uint32_t>(k)}, 1000
        );
    }
    Percentiles p;
    p.reserve(kIters);
    for (int i = 0; i < kIters; ++i) {
        auto const bid = OrderId{static_cast<std::uint64_t>(i) + 100};
        std::uint64_t const t0 = now_ticks();
        m.on_new(bid, Side::Buy, OrderType::Ioc, Price4{101000}, 100);
        std::uint64_t const t1 = now_ticks();
        p.add(ticks_to_ns(t1 - t0));
    }
    print("limit-insert-1-fill", p.report());
}

// Scenario 3: cancel a resting order.
void bench_cancel_resting() {
    Book book(Price4{100000}, 4000);
    OrderPool pool(kIters + 16);
    NullSink sink;
    Matcher m(book, pool, sink);
    for (int i = 0; i < kIters; ++i) {
        m.on_new(
            OrderId{static_cast<std::uint64_t>(i) + 1}, Side::Buy, OrderType::Limit,
            Price4{100000U + static_cast<std::uint32_t>(i % 2000)}, 100
        );
    }
    Percentiles p;
    p.reserve(kIters);
    for (int i = 0; i < kIters; ++i) {
        auto const id = OrderId{static_cast<std::uint64_t>(i) + 1};
        std::uint64_t const t0 = now_ticks();
        m.on_cancel(id);
        std::uint64_t const t1 = now_ticks();
        p.add(ticks_to_ns(t1 - t0));
    }
    print("cancel-resting", p.report());
}

}  // namespace

int main() {
    std::printf("# lazerbook bench_matcher (%d iterations each)\n", kIters);
    std::printf("# clock calibrated at %.3f GHz\n", tsc_hz_calibrated() / 1e9);
    bench_insert_no_match();
    bench_insert_one_fill();
    bench_cancel_resting();
    return 0;
}
