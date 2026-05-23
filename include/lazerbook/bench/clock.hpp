#ifndef LAZERBOOK_BENCH_CLOCK_HPP
#define LAZERBOOK_BENCH_CLOCK_HPP

#include <chrono>
#include <cstdint>

#if defined(__x86_64__)
#include <x86intrin.h>
#endif

namespace lazerbook::bench {

// Cheapest monotonic counter available. On x86 this is the timestamp counter
// (rdtscp, which serialises against earlier loads); elsewhere it falls back to
// steady_clock, whose resolution floors the measurable latency (~42 ns/tick on
// Apple Silicon).
[[nodiscard]] inline std::uint64_t now_ticks() noexcept {
#if defined(__x86_64__)
    unsigned aux = 0;
    return __rdtscp(&aux);
#else
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

// Ticks per second, calibrated once by spinning ~10 ms against steady_clock.
[[nodiscard]] inline double tsc_hz_calibrated() noexcept {
    static double const hz = [] {
#if defined(__x86_64__)
        using clock = std::chrono::steady_clock;
        auto const t0 = clock::now();
        std::uint64_t const c0 = now_ticks();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count() < 10
        ) {
        }
        std::uint64_t const c1 = now_ticks();
        auto const t1 = clock::now();
        double const secs = std::chrono::duration<double>(t1 - t0).count();
        return static_cast<double>(c1 - c0) / secs;
#else
        // steady_clock ticks are already nanoseconds on libc++/libstdc++.
        using period = std::chrono::steady_clock::period;
        return static_cast<double>(period::den) / static_cast<double>(period::num);
#endif
    }();
    return hz;
}

[[nodiscard]] inline double ticks_to_ns(std::uint64_t ticks) noexcept {
    double const hz = tsc_hz_calibrated();
    return static_cast<double>(ticks) * 1.0e9 / hz;
}

}  // namespace lazerbook::bench

#endif  // LAZERBOOK_BENCH_CLOCK_HPP
