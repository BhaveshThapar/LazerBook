#ifndef LAZERBOOK_TYPES_HPP
#define LAZERBOOK_TYPES_HPP

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace lazerbook {

// Buy/Sell side; underlying char matches the NASDAQ ITCH wire byte.
enum class Side : char { Buy = 'B', Sell = 'S' };

// Strong id / price / time types. Opaque enums prevent accidental mixing.
enum class OrderId : std::uint64_t {};
enum class Price4 : std::uint32_t {};     // price in 1/10000 of a dollar
enum class Timestamp : std::uint64_t {};  // nanoseconds since midnight ET

// 8-byte right-padded ASCII stock symbol (NASDAQ convention).
struct Symbol {
    std::array<char, 8> bytes{};

    [[nodiscard]] std::string_view view() const noexcept {
        std::size_t len = 0;
        while (len < bytes.size() && bytes[len] != ' ' && bytes[len] != '\0') {
            ++len;
        }
        return {bytes.data(), len};
    }

    [[nodiscard]] friend bool operator==(Symbol const&, Symbol const&) = default;
};

// Scalar representation of T: T itself for integers, the underlying type for
// enums. Deferred so underlying_type_t is never instantiated on a non-enum.
template <typename T, bool = std::is_enum_v<T>>
struct ScalarRep {
    using type = T;
};
template <typename T>
struct ScalarRep<T, true> {
    using type = std::underlying_type_t<T>;
};
template <typename T>
using scalar_rep_t = typename ScalarRep<T>::type;

// --- Big-endian decode helpers --------------------------------------------

template <typename T>
[[nodiscard]] inline T read_be(std::uint8_t const* p) noexcept {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    using U = scalar_rep_t<T>;
    U value{};
    std::memcpy(&value, p, sizeof(U));
    if constexpr (std::endian::native == std::endian::little) {
        value = std::byteswap(value);
    }
    return static_cast<T>(value);
}

// 6-byte big-endian unsigned (ITCH timestamps).
[[nodiscard]] inline std::uint64_t read_be48(std::uint8_t const* p) noexcept {
    std::uint64_t value = 0;
    for (int i = 0; i < 6; ++i) {
        value = (value << 8) | static_cast<std::uint64_t>(p[i]);
    }
    return value;
}

[[nodiscard]] inline Symbol read_symbol(std::uint8_t const* p) noexcept {
    Symbol s;
    std::memcpy(s.bytes.data(), p, s.bytes.size());
    return s;
}

// --- Big-endian encode helpers --------------------------------------------

template <typename T>
inline void write_be(std::uint8_t* p, T v) noexcept {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    using U = scalar_rep_t<T>;
    auto value = static_cast<U>(v);
    if constexpr (std::endian::native == std::endian::little) {
        value = std::byteswap(value);
    }
    std::memcpy(p, &value, sizeof(U));
}

inline void write_be48(std::uint8_t* p, std::uint64_t v) noexcept {
    for (int i = 5; i >= 0; --i) {
        p[i] = static_cast<std::uint8_t>(v & 0xFFU);
        v >>= 8;
    }
}

inline void write_symbol(std::uint8_t* p, Symbol const& s) noexcept {
    std::memcpy(p, s.bytes.data(), s.bytes.size());
}

// Convenience scalar accessors for the strong enum types.
[[nodiscard]] constexpr std::uint64_t value_of(OrderId id) noexcept {
    return static_cast<std::uint64_t>(id);
}
[[nodiscard]] constexpr std::uint32_t value_of(Price4 p) noexcept {
    return static_cast<std::uint32_t>(p);
}
[[nodiscard]] constexpr std::uint64_t value_of(Timestamp t) noexcept {
    return static_cast<std::uint64_t>(t);
}

}  // namespace lazerbook

#endif  // LAZERBOOK_TYPES_HPP
