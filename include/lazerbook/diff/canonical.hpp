#ifndef LAZERBOOK_DIFF_CANONICAL_HPP
#define LAZERBOOK_DIFF_CANONICAL_HPP

#include <lazerbook/types.hpp>

#include <cstdint>

// Engine-agnostic representation of a match, plus the command alphabet that both
// drivers consume. Deliberately excludes timestamps, match numbers, and sequence
// numbers so two engines can be compared on economic outcome alone.
namespace lazerbook::diff {

struct CanonicalFill {
    std::uint64_t aggressor_id{};
    std::uint64_t passive_id{};
    std::uint32_t price_ticks{};
    std::uint32_t shares{};
    Side aggressor_side{Side::Buy};

    [[nodiscard]] friend bool operator==(CanonicalFill const&, CanonicalFill const&) = default;
};

struct Command {
    enum class Kind : std::uint8_t {
        NewLimit,
        NewMarket,
        NewIoc,
        NewFok,
        Cancel,
        Modify,
    };

    Kind kind{Kind::NewLimit};
    OrderId id{};
    Side side{Side::Buy};
    Price4 price{};
    std::uint32_t shares{};
    OrderId modify_new_id{};
    Price4 modify_new_price{};
    std::uint32_t modify_new_shares{};
};

}  // namespace lazerbook::diff

#endif  // LAZERBOOK_DIFF_CANONICAL_HPP
