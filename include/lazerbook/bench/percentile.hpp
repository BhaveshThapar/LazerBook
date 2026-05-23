#ifndef LAZERBOOK_BENCH_PERCENTILE_HPP
#define LAZERBOOK_BENCH_PERCENTILE_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lazerbook::bench {

struct Report {
    double p50 = 0;
    double p90 = 0;
    double p99 = 0;
    double p999 = 0;
    double p9999 = 0;
    double max = 0;
    double mean = 0;
    std::size_t n = 0;
};

// Records raw nanosecond samples; report() sorts a copy and extracts the tail.
class Percentiles {
   public:
    void add(double ns) { samples_.push_back(ns); }
    void reserve(std::size_t n) { samples_.reserve(n); }
    [[nodiscard]] std::size_t size() const noexcept { return samples_.size(); }

    [[nodiscard]] Report report() const {
        Report r;
        r.n = samples_.size();
        if (samples_.empty()) {
            return r;
        }
        std::vector<double> s = samples_;
        std::sort(s.begin(), s.end());
        auto quantile = [&s](double q) {
            auto idx = static_cast<std::size_t>(q * static_cast<double>(s.size() - 1));
            return s[idx];
        };
        r.p50 = quantile(0.50);
        r.p90 = quantile(0.90);
        r.p99 = quantile(0.99);
        r.p999 = quantile(0.999);
        r.p9999 = quantile(0.9999);
        r.max = s.back();
        double sum = 0;
        for (double v : s) {
            sum += v;
        }
        r.mean = sum / static_cast<double>(s.size());
        return r;
    }

   private:
    std::vector<double> samples_;
};

}  // namespace lazerbook::bench

#endif  // LAZERBOOK_BENCH_PERCENTILE_HPP
