#ifndef LAZERBOOK_BENCH_CLOCK_HPP
#define LAZERBOOK_BENCH_CLOCK_HPP

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

#if defined(__x86_64__)
#include <cpuid.h>
#include <x86intrin.h>
#endif

#if defined(__linux__)
#include <ctime>
#endif

// Timestamp-counter access for the benchmarks. Three read flavours, because the
// right amount of serialisation differs between a microbenchmark bracketing a
// single call and a pipeline stamping every message:
//
//   rdtsc_begin/rdtsc_end  fenced, for timing a short region precisely
//   rdtsc_raw              unfenced, for hot paths where ~8ns of fence cost
//                          would be a quarter of the per-message budget
//
// All of it is x86-only; elsewhere these fall back to steady_clock, whose
// resolution (~42ns/tick on Apple Silicon) floors what can be measured.
namespace lazerbook::bench {

// --- Raw counter reads ------------------------------------------------------

// Unfenced. Cheapest read (~12-15 cycles on Raptor Lake). Use where a few
// cycles of skew do not matter and the fence cost would.
[[nodiscard]] inline std::uint64_t rdtsc_raw() noexcept {
#if defined(__x86_64__)
    return __rdtsc();
#else
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

// Start of a timed region. LFENCE before, so prior work has retired; LFENCE
// after, so nothing floats above the counter read. Intel sanctions
// LFENCE;RDTSC as the modern replacement for CPUID;RDTSC, which costs 100-200
// cycles and can trap to a hypervisor.
[[nodiscard]] inline std::uint64_t rdtsc_begin() noexcept {
#if defined(__x86_64__)
    _mm_lfence();
    std::uint64_t const t = __rdtsc();
    _mm_lfence();
    return t;
#else
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

// End of a timed region. RDTSCP already waits for prior instructions to have
// executed; the trailing LFENCE stops later work from floating above it.
[[nodiscard]] inline std::uint64_t rdtsc_end() noexcept {
#if defined(__x86_64__)
    unsigned aux = 0;
    std::uint64_t const t = __rdtscp(&aux);
    _mm_lfence();
    return t;
#else
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

// Retained for existing call sites; equivalent to the old behaviour.
[[nodiscard]] inline std::uint64_t now_ticks() noexcept { return rdtsc_end(); }

// --- Capability detection ---------------------------------------------------

struct TscCapabilities {
    bool x86 = false;
    bool invariant_tsc = false;  // CPUID 0x80000007 EDX[8]
    bool rdtscp = false;         // CPUID 0x80000001 EDX[27]
    std::uint64_t cpuid_15_hz = 0;
    std::uint64_t cpuid_16_hz = 0;
};

// A non-invariant TSC changes rate with P-state, which silently invalidates
// every latency number derived from it. Benchmarks check this and refuse.
[[nodiscard]] inline TscCapabilities detect_tsc() noexcept {
    TscCapabilities c;
#if defined(__x86_64__)
    c.x86 = true;
    unsigned a = 0;
    unsigned b = 0;
    unsigned cx = 0;
    unsigned d = 0;
    if (__get_cpuid(0x80000000U, &a, &b, &cx, &d) != 0 && a >= 0x80000007U) {
        if (__get_cpuid(0x80000007U, &a, &b, &cx, &d) != 0) {
            c.invariant_tsc = (d & (1U << 8)) != 0;
        }
    }
    if (__get_cpuid(0x80000001U, &a, &b, &cx, &d) != 0) {
        c.rdtscp = (d & (1U << 27)) != 0;
    }
    if (__get_cpuid(0x15U, &a, &b, &cx, &d) != 0 && a != 0 && b != 0) {
        // ECX is the crystal frequency; client parts often report 0, where the
        // 38.4 MHz reference clock is the documented fallback.
        std::uint64_t const crystal = (cx != 0) ? cx : 38400000ULL;
        c.cpuid_15_hz = crystal * b / a;
    }
    if (__get_cpuid(0x16U, &a, &b, &cx, &d) != 0) {
        c.cpuid_16_hz = static_cast<std::uint64_t>(a) * 1000000ULL;
    }
#endif
    return c;
}

// --- Calibration ------------------------------------------------------------

struct TscCalibration {
    double hz = 0.0;
    double ppm_spread = 0.0;  // round-to-round agreement
    std::uint64_t accepted = 0;
    std::uint64_t rejected = 0;
};

namespace detail {

// Reference clock must be unslewed. CLOCK_MONOTONIC is NTP/adjtime-adjusted by
// up to ~500ppm, which corrupts a ratio measurement; CLOCK_MONOTONIC_RAW is not.
[[nodiscard]] inline std::uint64_t ref_ns() noexcept {
#if defined(__linux__)
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<std::uint64_t>(ts.tv_nsec);
#else
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch()
    )
                                          .count());
#endif
}

struct Sample {
    std::uint64_t tsc;
    std::uint64_t ns;
    std::uint64_t bracket;  // tsc uncertainty of this reference read
};

}  // namespace detail

// Least-squares fit of tsc against CLOCK_MONOTONIC_RAW over a window, with
// interrupt rejection: every reference read is sandwiched between two counter
// reads, and samples whose bracket is an outlier are discarded.
[[nodiscard]] inline TscCalibration calibrate_tsc(int window_ms = 200, int rounds = 3) noexcept {
    TscCalibration out;
#if defined(__x86_64__)
    double best = 0.0;
    double worst = 0.0;
    for (int round = 0; round < rounds; ++round) {
        std::vector<detail::Sample> samples;
        samples.reserve(2048);
        std::uint64_t const start_ns = detail::ref_ns();
        std::uint64_t const window_ns = static_cast<std::uint64_t>(window_ms) * 1000000ULL;
        while (detail::ref_ns() - start_ns < window_ns) {
            std::uint64_t const t0 = rdtsc_raw();
            std::uint64_t const ns = detail::ref_ns();
            std::uint64_t const t1 = rdtsc_raw();
            samples.push_back({(t0 / 2) + (t1 / 2), ns, t1 - t0});
        }
        if (samples.size() < 16) {
            continue;
        }
        // Reject reads that took an interrupt: bracket > 4x the median bracket.
        std::vector<std::uint64_t> brackets;
        brackets.reserve(samples.size());
        for (auto const& s : samples) {
            brackets.push_back(s.bracket);
        }
        std::nth_element(
            brackets.begin(), brackets.begin() + (brackets.size() / 2), brackets.end()
        );
        std::uint64_t const limit = brackets[brackets.size() / 2] * 4 + 64;

        // Least squares through the accepted points.
        double n = 0.0;
        double sx = 0.0;
        double sy = 0.0;
        double sxx = 0.0;
        double sxy = 0.0;
        for (auto const& s : samples) {
            if (s.bracket > limit) {
                ++out.rejected;
                continue;
            }
            double const x = static_cast<double>(s.ns - samples.front().ns) * 1e-9;
            double const y = static_cast<double>(s.tsc - samples.front().tsc);
            n += 1.0;
            sx += x;
            sy += y;
            sxx += x * x;
            sxy += x * y;
            ++out.accepted;
        }
        double const denom = (n * sxx) - (sx * sx);
        if (n < 8.0 || denom <= 0.0) {
            continue;
        }
        double const hz = ((n * sxy) - (sx * sy)) / denom;
        if (hz <= 0.0) {
            continue;
        }
        out.hz = hz;
        best = (best == 0.0) ? hz : std::min(best, hz);
        worst = std::max(worst, hz);
    }
    if (best > 0.0) {
        out.ppm_spread = (worst - best) / best * 1e6;
    }
#else
    (void)window_ms;
    (void)rounds;
    using period = std::chrono::steady_clock::period;
    out.hz = static_cast<double>(period::den) / static_cast<double>(period::num);
#endif
    return out;
}

// Ticks per second, calibrated once.
[[nodiscard]] inline double tsc_hz_calibrated() noexcept {
    static double const hz = [] {
        TscCalibration const c = calibrate_tsc();
        if (c.hz > 0.0) {
            return c.hz;
        }
#if defined(__x86_64__)
        // Calibration failed; fall back to whatever CPUID reports.
        TscCapabilities const caps = detect_tsc();
        if (caps.cpuid_15_hz != 0) {
            return static_cast<double>(caps.cpuid_15_hz);
        }
        if (caps.cpuid_16_hz != 0) {
            return static_cast<double>(caps.cpuid_16_hz);
        }
        return 1e9;
#else
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

// Measured cost of an empty timed bracket, so a reader can subtract it.
[[nodiscard]] inline std::uint64_t clock_overhead_ticks(int iters = 4096) noexcept {
    std::uint64_t best = ~std::uint64_t{0};
    for (int i = 0; i < iters; ++i) {
        std::uint64_t const t0 = rdtsc_begin();
        std::uint64_t const t1 = rdtsc_end();
        best = std::min(best, t1 - t0);
    }
    return best;
}

}  // namespace lazerbook::bench

#endif  // LAZERBOOK_BENCH_CLOCK_HPP
