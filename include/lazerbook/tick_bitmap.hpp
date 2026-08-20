#ifndef LAZERBOOK_TICK_BITMAP_HPP
#define LAZERBOOK_TICK_BITMAP_HPP

#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lazerbook {

// Three-level occupancy bitmap over price ticks.
//
// Replaces the linear rescan the book did when its best level emptied, which
// was O(num_ticks) -- 4000 iterations over 128KB of PriceLevel in the default
// configuration, and the source of the multi-microsecond tail outliers.
//
// Level 0 has one bit per tick, level 1 one bit per level-0 word, level 2 one
// bit per level-1 word. Three levels index 64^3 = 262144 ticks with a single
// level-2 word; beyond that the top level is scanned linearly, which is still
// 1/262144th of the work the old scan did.
//
// Set and clear are a handful of ORs/ANDs; nearest-set-bit queries are two or
// three dependent loads plus a countr_zero / countl_zero.
class TickBitmap {
   public:
    static constexpr std::uint32_t kNone = 0xFFFFFFFFU;

    explicit TickBitmap(std::uint32_t num_bits)
        : nbits_(num_bits),
          l0_(words_for(num_bits), 0),
          l1_(words_for(static_cast<std::uint32_t>(words_for(num_bits))), 0),
          l2_(words_for(static_cast<std::uint32_t>(
                  words_for(static_cast<std::uint32_t>(words_for(num_bits)))
              )),
              0) {}

    [[nodiscard]] std::uint32_t size() const noexcept { return nbits_; }

    [[nodiscard]] bool test(std::uint32_t i) const noexcept { return (l0_[i >> 6] & bit(i)) != 0; }

    void set(std::uint32_t i) noexcept {
        l0_[i >> 6] |= bit(i);
        l1_[i >> 12] |= bit(i >> 6);
        l2_[i >> 18] |= bit(i >> 12);
    }

    // Clearing only propagates upward while a whole word has emptied, so the
    // common case touches one word.
    void clear(std::uint32_t i) noexcept {
        std::uint32_t const w0 = i >> 6;
        l0_[w0] &= ~bit(i);
        if (l0_[w0] != 0) {
            return;
        }
        std::uint32_t const w1 = i >> 12;
        l1_[w1] &= ~bit(w0);
        if (l1_[w1] != 0) {
            return;
        }
        l2_[i >> 18] &= ~bit(w1);
    }

    [[nodiscard]] bool empty() const noexcept {
        for (std::uint64_t w : l2_) {
            if (w != 0) {
                return false;
            }
        }
        return true;
    }

    // Lowest set bit at or above `from`, or kNone.
    [[nodiscard]] std::uint32_t lowest_ge(std::uint32_t from) const noexcept {
        if (from >= nbits_) {
            return kNone;
        }
        std::uint32_t const w0 = from >> 6;
        if (std::uint64_t const w = l0_[w0] & at_or_above(from & 63U); w != 0) {
            return (w0 << 6) | ctz(w);
        }
        std::uint32_t const w1 = w0 >> 6;
        if (std::uint64_t const w = l1_[w1] & above(w0 & 63U); w != 0) {
            return from_l1_word((w1 << 6) | ctz(w));
        }
        std::uint32_t const w2 = w1 >> 6;
        if (std::uint64_t const w = l2_[w2] & above(w1 & 63U); w != 0) {
            return from_l2_word((w2 << 6) | ctz(w));
        }
        for (std::size_t i = static_cast<std::size_t>(w2) + 1; i < l2_.size(); ++i) {
            if (l2_[i] != 0) {
                return from_l2_word((static_cast<std::uint32_t>(i) << 6) | ctz(l2_[i]));
            }
        }
        return kNone;
    }

    // Highest set bit at or below `from`, or kNone.
    [[nodiscard]] std::uint32_t highest_le(std::uint32_t from) const noexcept {
        if (nbits_ == 0) {
            return kNone;
        }
        if (from >= nbits_) {
            from = nbits_ - 1;
        }
        std::uint32_t const w0 = from >> 6;
        if (std::uint64_t const w = l0_[w0] & at_or_below(from & 63U); w != 0) {
            return (w0 << 6) | msb(w);
        }
        std::uint32_t const w1 = w0 >> 6;
        if (std::uint64_t const w = l1_[w1] & below(w0 & 63U); w != 0) {
            return from_l1_word_high((w1 << 6) | msb(w));
        }
        std::uint32_t const w2 = w1 >> 6;
        if (std::uint64_t const w = l2_[w2] & below(w1 & 63U); w != 0) {
            return from_l2_word_high((w2 << 6) | msb(w));
        }
        for (std::size_t i = w2; i-- > 0;) {
            if (l2_[i] != 0) {
                return from_l2_word_high((static_cast<std::uint32_t>(i) << 6) | msb(l2_[i]));
            }
        }
        return kNone;
    }

    [[nodiscard]] std::uint32_t lowest() const noexcept { return lowest_ge(0); }
    [[nodiscard]] std::uint32_t highest() const noexcept {
        return (nbits_ == 0) ? kNone : highest_le(nbits_ - 1);
    }

   private:
    static constexpr std::size_t words_for(std::uint32_t bits) noexcept {
        return (static_cast<std::size_t>(bits) + 63) / 64;
    }
    static constexpr std::uint64_t bit(std::uint32_t i) noexcept {
        return std::uint64_t{1} << (i & 63U);
    }
    static std::uint32_t ctz(std::uint64_t w) noexcept {
        return static_cast<std::uint32_t>(std::countr_zero(w));
    }
    static std::uint32_t msb(std::uint64_t w) noexcept {
        return 63U - static_cast<std::uint32_t>(std::countl_zero(w));
    }
    // Bit masks within a 64-bit word.
    static constexpr std::uint64_t at_or_above(std::uint32_t r) noexcept { return ~0ULL << r; }
    static constexpr std::uint64_t above(std::uint32_t r) noexcept {
        return (r == 63U) ? 0ULL : (~0ULL << (r + 1));
    }
    static constexpr std::uint64_t at_or_below(std::uint32_t r) noexcept {
        return (r == 63U) ? ~0ULL : ((1ULL << (r + 1)) - 1);
    }
    static constexpr std::uint64_t below(std::uint32_t r) noexcept {
        return (r == 0U) ? 0ULL : ((1ULL << r) - 1);
    }

    // Descend from a known-nonempty summary index to the actual bit.
    [[nodiscard]] std::uint32_t from_l1_word(std::uint32_t w0) const noexcept {
        return (w0 << 6) | ctz(l0_[w0]);
    }
    [[nodiscard]] std::uint32_t from_l2_word(std::uint32_t w1) const noexcept {
        return from_l1_word((w1 << 6) | ctz(l1_[w1]));
    }
    [[nodiscard]] std::uint32_t from_l1_word_high(std::uint32_t w0) const noexcept {
        return (w0 << 6) | msb(l0_[w0]);
    }
    [[nodiscard]] std::uint32_t from_l2_word_high(std::uint32_t w1) const noexcept {
        return from_l1_word_high((w1 << 6) | msb(l1_[w1]));
    }

    std::uint32_t nbits_;
    std::vector<std::uint64_t> l0_;
    std::vector<std::uint64_t> l1_;
    std::vector<std::uint64_t> l2_;
};

}  // namespace lazerbook

#endif  // LAZERBOOK_TICK_BITMAP_HPP
