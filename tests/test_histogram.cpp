#include <lazerbook/bench/clock.hpp>
#include <lazerbook/bench/histogram.hpp>

#include <algorithm>
#include <cstdint>
#include <doctest/doctest.h>
#include <vector>

using lazerbook::bench::Histogram;

namespace {

// Small SubBits makes the octave boundaries easy to reason about by hand:
// bucket 0 covers [0,16) exactly, then each octave keeps 8 slots.
using H4 = Histogram<4, 16>;

}  // namespace

TEST_CASE("histogram: empty reports zeroes") {
    H4 h;
    CHECK(h.count() == 0);
    CHECK(h.max() == 0);
    CHECK(h.min() == 0);
    CHECK(h.mean() == doctest::Approx(0.0));
    CHECK(h.percentile(0.5) == 0);
}

TEST_CASE("histogram: sub-bucket range is exact") {
    // Below 2^SubBits every value gets its own slot, so no error at all.
    H4 h;
    for (std::uint64_t v = 0; v < 16; ++v) {
        h.record(v);
    }
    CHECK(h.count() == 16);
    CHECK(h.min() == 0);
    CHECK(h.max() == 15);
    for (std::uint64_t v = 0; v < 16; ++v) {
        CHECK(H4::index_of(v) == v);
        CHECK(H4::value_at(static_cast<std::size_t>(v)) == v);
    }
}

TEST_CASE("histogram: slot indices are strictly monotonic in value") {
    // A gap or an overlap in the layout would silently misreport every tail.
    std::size_t prev = H4::index_of(0);
    for (std::uint64_t v = 1; v < 200000; ++v) {
        std::size_t const idx = H4::index_of(v);
        CHECK(idx >= prev);
        prev = idx;
    }
}

TEST_CASE("histogram: value_at is the inverse of index_of at slot boundaries") {
    for (std::size_t i = 0; i < H4::kSlots; ++i) {
        std::uint64_t const lo = H4::value_at(i);
        CHECK(H4::index_of(lo) == i);
    }
}

TEST_CASE("histogram: relative error stays within the octave bound") {
    // Each octave keeps 2^(SubBits-1) slots, so error < 2^-(SubBits-1).
    constexpr double kBound = 1.0 / 8.0;  // SubBits == 4
    for (std::uint64_t v = 1; v < 100000; v = (v * 7 / 5) + 1) {
        std::size_t const idx = H4::index_of(v);
        std::uint64_t const lo = H4::value_at(idx);
        REQUIRE(lo <= v);
        double const err = static_cast<double>(v - lo) / static_cast<double>(v);
        CHECK(err < kBound);
    }
}

TEST_CASE("histogram: percentiles bracket a known uniform distribution") {
    lazerbook::bench::LatencyHistogram h;
    for (std::uint64_t v = 1; v <= 10000; ++v) {
        h.record(v);
    }
    CHECK(h.count() == 10000);
    CHECK(h.min() == 1);
    CHECK(h.max() == 10000);
    // Default SubBits=10 gives <0.2% error, so these are tight brackets.
    CHECK(h.percentile(0.50) >= 4990);
    CHECK(h.percentile(0.50) <= 5020);
    CHECK(h.percentile(0.99) >= 9890);
    CHECK(h.percentile(0.99) <= 9920);
    CHECK(h.percentile(1.0) <= 10000);
}

TEST_CASE("histogram: percentile never exceeds the recorded max") {
    lazerbook::bench::LatencyHistogram h;
    h.record(5);
    h.record(7);
    h.record(9);
    CHECK(h.percentile(1.0) <= 9);
    CHECK(h.percentile(0.999999) <= 9);
}

TEST_CASE("histogram: a single sample reports itself at every percentile") {
    lazerbook::bench::LatencyHistogram h;
    h.record(1234);
    CHECK(h.count() == 1);
    CHECK(h.percentile(0.5) >= 1234);
    CHECK(h.percentile(0.99) >= 1234);
    CHECK(h.max() == 1234);
}

TEST_CASE("histogram: matches an exact sorted reference on random input") {
    // Ground truth: keep every sample, sort, index. The histogram must land
    // within its stated resolution of that.
    lazerbook::bench::LatencyHistogram h;
    std::vector<std::uint64_t> exact;
    std::uint64_t state = 0xABCDEF;
    for (int i = 0; i < 50000; ++i) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        std::uint64_t const v = (state % 20000) + 1;
        h.record(v);
        exact.push_back(v);
    }
    std::sort(exact.begin(), exact.end());
    auto quant = [&exact](double q) {
        auto idx = static_cast<std::size_t>(q * static_cast<double>(exact.size() - 1));
        return exact[idx];
    };
    for (double q : {0.5, 0.9, 0.99, 0.999}) {
        auto const got = static_cast<double>(h.percentile(q));
        auto const want = static_cast<double>(quant(q));
        CHECK(got >= want * 0.99);
        CHECK(got <= want * 1.01);
    }
}

TEST_CASE("histogram: add merges two histograms") {
    lazerbook::bench::LatencyHistogram a;
    lazerbook::bench::LatencyHistogram b;
    for (std::uint64_t v = 1; v <= 100; ++v) {
        a.record(v);
    }
    for (std::uint64_t v = 101; v <= 200; ++v) {
        b.record(v);
    }
    a.add(b);
    CHECK(a.count() == 200);
    CHECK(a.min() == 1);
    CHECK(a.max() == 200);
}

TEST_CASE("histogram: reset clears everything") {
    lazerbook::bench::LatencyHistogram h;
    h.record(42);
    h.reset();
    CHECK(h.count() == 0);
    CHECK(h.max() == 0);
    CHECK(h.percentile(0.5) == 0);
}

TEST_CASE("histogram: record_corrected credits omitted samples") {
    // A 1000-tick stall against a 100-tick expected interval stands for one
    // real sample plus nine that were prevented from being issued.
    lazerbook::bench::LatencyHistogram h;
    h.record_corrected(1000, 100);
    CHECK(h.count() == 10);
    CHECK(h.max() == 1000);

    lazerbook::bench::LatencyHistogram plain;
    plain.record_corrected(50, 100);  // under the interval: no correction
    CHECK(plain.count() == 1);

    lazerbook::bench::LatencyHistogram zero;
    zero.record_corrected(500, 0);  // no expected interval: no correction
    CHECK(zero.count() == 1);
}

TEST_CASE("histogram: very large values saturate into the last slot") {
    lazerbook::bench::LatencyHistogram h;
    h.record(~std::uint64_t{0});
    CHECK(h.count() == 1);
    CHECK(h.max() == ~std::uint64_t{0});
}

// --- clock ------------------------------------------------------------------

TEST_CASE("clock: counter is monotonic across successive reads") {
    std::uint64_t prev = lazerbook::bench::rdtsc_begin();
    for (int i = 0; i < 1000; ++i) {
        std::uint64_t const now = lazerbook::bench::rdtsc_end();
        CHECK(now >= prev);
        prev = now;
    }
}

TEST_CASE("clock: calibration yields a plausible frequency") {
    double const hz = lazerbook::bench::tsc_hz_calibrated();
    // Anything outside 100MHz..100GHz means calibration is broken.
    CHECK(hz > 1e8);
    CHECK(hz < 1e11);
}

TEST_CASE("clock: ticks_to_ns is linear and anchored at zero") {
    CHECK(lazerbook::bench::ticks_to_ns(0) == doctest::Approx(0.0));
    double const a = lazerbook::bench::ticks_to_ns(1000);
    double const b = lazerbook::bench::ticks_to_ns(2000);
    CHECK(b == doctest::Approx(a * 2.0).epsilon(1e-9));
}

TEST_CASE("clock: capability probe reports coherently") {
    auto const caps = lazerbook::bench::detect_tsc();
#if defined(__x86_64__)
    CHECK(caps.x86);
#else
    // On non-x86 there is no TSC to describe.
    CHECK_FALSE(caps.x86);
    CHECK_FALSE(caps.invariant_tsc);
#endif
}
