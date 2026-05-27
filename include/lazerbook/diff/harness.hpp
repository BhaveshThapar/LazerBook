#ifndef LAZERBOOK_DIFF_HARNESS_HPP
#define LAZERBOOK_DIFF_HARNESS_HPP

#include <lazerbook/diff/canonical.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace lazerbook::diff {

struct DiffResult {
    bool equal = true;
    std::size_t divergent_at = 0;  // index of first differing fill (valid if !equal)
    std::size_t commands_processed = 0;
    std::size_t fills_a = 0;
    std::size_t fills_b = 0;
};

// Feeds the same command sequence to two drivers and compares their fill streams
// position by position, stopping at the first divergence. Each driver is a
// callable invoked as driver(span<Command const>, vector<CanonicalFill>& out).
template <typename DriverA, typename DriverB>
DiffResult run_differential(
    std::span<Command const> commands, DriverA&& driver_a, DriverB&& driver_b,
    std::vector<CanonicalFill>& out_a, std::vector<CanonicalFill>& out_b
) {
    out_a.clear();
    out_b.clear();
    driver_a(commands, out_a);
    driver_b(commands, out_b);

    DiffResult r;
    r.commands_processed = commands.size();
    r.fills_a = out_a.size();
    r.fills_b = out_b.size();

    std::size_t const common = std::min(out_a.size(), out_b.size());
    for (std::size_t i = 0; i < common; ++i) {
        if (!(out_a[i] == out_b[i])) {
            r.equal = false;
            r.divergent_at = i;
            return r;
        }
    }
    if (out_a.size() != out_b.size()) {
        r.equal = false;
        r.divergent_at = common;
    }
    return r;
}

}  // namespace lazerbook::diff

#endif  // LAZERBOOK_DIFF_HARNESS_HPP
