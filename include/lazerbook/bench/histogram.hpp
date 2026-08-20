#ifndef LAZERBOOK_BENCH_HISTOGRAM_HPP
#define LAZERBOOK_BENCH_HISTOGRAM_HPP

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace lazerbook::bench {

// Allocation-free log-linear histogram (HdrHistogram layout).
//
// Bucket 0 covers [0, 2^SubBits) linearly, one slot per value. Bucket k>0
// covers [2^(SubBits+k-1), 2^(SubBits+k)) with the low bit dropped k times, so
// each octave keeps 2^(SubBits-1) slots and relative error stays under
// 2^-(SubBits-1). At SubBits=10 that is <0.2%, i.e. sub-nanosecond resolution
// anywhere near a 247ns target.
//
// record() is a shift, a count-leading-zeros and one increment -- no branches
// on the common path, no allocation, ever. This matters because the histogram
// is written from inside the region being measured.
template <int SubBits = 10, int Octaves = 32>
class Histogram {
    static_assert(SubBits >= 2 && SubBits <= 16);
    static_assert(Octaves >= 2 && Octaves <= 56);

    static constexpr std::size_t kSub = std::size_t{1} << SubBits;
    static constexpr std::size_t kHalfSub = kSub >> 1;

   public:
    static constexpr std::size_t kSlots = kSub + (static_cast<std::size_t>(Octaves) - 1) * kHalfSub;

    [[nodiscard]] static constexpr std::size_t index_of(std::uint64_t v) noexcept {
        // v|1 keeps countl_zero defined at v==0 and maps 0 to slot 0.
        auto const msb = static_cast<unsigned>(63 - std::countl_zero(v | 1ULL));
        unsigned const bucket = (msb < static_cast<unsigned>(SubBits))
                                    ? 0U
                                    : (msb - static_cast<unsigned>(SubBits) + 1U);
        auto const sub = static_cast<std::size_t>(v >> bucket);
        std::size_t const idx =
            (bucket == 0U) ? sub : (static_cast<std::size_t>(bucket) * kHalfSub) + sub;
        return (idx < kSlots) ? idx : (kSlots - 1);
    }

    // Lowest value that lands in this slot -- used to turn a slot back into a
    // magnitude at report time. Inverse of index_of at slot boundaries.
    //
    // Note the -1: octave k occupies indices [k*kHalfSub + kHalfSub,
    // (k+1)*kHalfSub + kHalfSub), so idx/kHalfSub overshoots by exactly one.
    [[nodiscard]] static constexpr std::uint64_t value_at(std::size_t idx) noexcept {
        if (idx < kSub) {
            return idx;
        }
        auto const bucket = static_cast<unsigned>((idx / kHalfSub) - 1);
        auto const sub =
            static_cast<std::uint64_t>(idx - (static_cast<std::size_t>(bucket) * kHalfSub));
        return sub << bucket;
    }

    void record(std::uint64_t v) noexcept {
        ++counts_[index_of(v)];
        ++total_;
        sum_ += v;
        if (v > max_) {
            max_ = v;
        }
        if (v < min_) {
            min_ = v;
        }
    }

    // Closed-loop retrofit: credits the samples a stall would have delayed.
    // Only for call sites that cannot be converted to an open-loop schedule --
    // an open-loop harness records the true value directly and needs no
    // correction at all.
    void record_corrected(std::uint64_t v, std::uint64_t expected_interval) noexcept {
        record(v);
        if (expected_interval == 0) {
            return;
        }
        for (std::uint64_t m = v; m > expected_interval;) {
            m -= expected_interval;
            record(m);
        }
    }

    void add(Histogram const& other) noexcept {
        for (std::size_t i = 0; i < kSlots; ++i) {
            counts_[i] += other.counts_[i];
        }
        total_ += other.total_;
        sum_ += other.sum_;
        max_ = (other.max_ > max_) ? other.max_ : max_;
        min_ = (other.min_ < min_) ? other.min_ : min_;
    }

    void reset() noexcept {
        counts_.fill(0);
        total_ = 0;
        sum_ = 0;
        max_ = 0;
        min_ = ~std::uint64_t{0};
    }

    [[nodiscard]] std::uint64_t count() const noexcept { return total_; }
    [[nodiscard]] std::uint64_t max() const noexcept { return (total_ == 0) ? 0 : max_; }
    [[nodiscard]] std::uint64_t min() const noexcept { return (total_ == 0) ? 0 : min_; }
    [[nodiscard]] double mean() const noexcept {
        return (total_ == 0) ? 0.0 : static_cast<double>(sum_) / static_cast<double>(total_);
    }

    // Returns the upper bound of the slot holding the requested quantile, the
    // HdrHistogram convention: conservative, never flattering.
    [[nodiscard]] std::uint64_t percentile(double p) const noexcept {
        if (total_ == 0) {
            return 0;
        }
        if (p <= 0.0) {
            return min_;
        }
        // Ceil so p99 of 100 samples needs 99 samples counted, not 98.
        auto want = static_cast<std::uint64_t>(
            static_cast<double>(total_) * (p > 1.0 ? 1.0 : p) + 0.999999999
        );
        if (want == 0) {
            want = 1;
        }
        if (want > total_) {
            want = total_;
        }
        std::uint64_t seen = 0;
        for (std::size_t i = 0; i < kSlots; ++i) {
            seen += counts_[i];
            if (seen >= want) {
                std::uint64_t const lo = value_at(i);
                std::uint64_t const next = (i + 1 < kSlots) ? value_at(i + 1) : (lo + 1);
                std::uint64_t const hi = (next > lo) ? (next - 1) : lo;
                return (hi > max_) ? max_ : hi;
            }
        }
        return max_;
    }

   private:
    std::array<std::uint64_t, kSlots> counts_{};
    std::uint64_t total_{0};
    std::uint64_t sum_{0};
    std::uint64_t max_{0};
    std::uint64_t min_{~std::uint64_t{0}};
};

using LatencyHistogram = Histogram<>;

}  // namespace lazerbook::bench

#endif  // LAZERBOOK_BENCH_HISTOGRAM_HPP
