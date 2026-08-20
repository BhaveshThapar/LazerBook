#include <lazerbook/tick_bitmap.hpp>

#include <cstdint>
#include <doctest/doctest.h>
#include <set>
#include <vector>

using lazerbook::TickBitmap;

namespace {

// Naive reference: a sorted set answering the same queries by scanning.
struct RefBitmap {
    std::set<std::uint32_t> bits;
    std::uint32_t n;

    explicit RefBitmap(std::uint32_t nbits) : n(nbits) {}

    void set(std::uint32_t i) { bits.insert(i); }
    void clear(std::uint32_t i) { bits.erase(i); }
    [[nodiscard]] bool test(std::uint32_t i) const { return bits.count(i) != 0; }
    [[nodiscard]] bool empty() const { return bits.empty(); }

    [[nodiscard]] std::uint32_t lowest_ge(std::uint32_t from) const {
        auto it = bits.lower_bound(from);
        return (it == bits.end()) ? TickBitmap::kNone : *it;
    }
    [[nodiscard]] std::uint32_t highest_le(std::uint32_t from) const {
        auto it = bits.upper_bound(from);
        return (it == bits.begin()) ? TickBitmap::kNone : *std::prev(it);
    }
};

}  // namespace

TEST_CASE("tick bitmap: empty reports nothing set") {
    TickBitmap b(4096);
    CHECK(b.empty());
    CHECK(b.lowest() == TickBitmap::kNone);
    CHECK(b.highest() == TickBitmap::kNone);
    CHECK(b.lowest_ge(0) == TickBitmap::kNone);
    CHECK(b.highest_le(4095) == TickBitmap::kNone);
    CHECK_FALSE(b.test(0));
}

TEST_CASE("tick bitmap: set and test round-trip") {
    TickBitmap b(4096);
    for (std::uint32_t i : {0U, 1U, 63U, 64U, 65U, 4094U, 4095U}) {
        b.set(i);
        CHECK(b.test(i));
    }
    CHECK_FALSE(b.empty());
    CHECK(b.lowest() == 0);
    CHECK(b.highest() == 4095);
}

TEST_CASE("tick bitmap: word boundaries behave") {
    // 63/64 and 4095/4096 are where off-by-one masking bugs live.
    TickBitmap b(4096);
    b.set(63);
    CHECK(b.lowest_ge(0) == 63);
    CHECK(b.lowest_ge(63) == 63);
    CHECK(b.lowest_ge(64) == TickBitmap::kNone);
    CHECK(b.highest_le(63) == 63);
    CHECK(b.highest_le(62) == TickBitmap::kNone);
    CHECK(b.highest_le(4095) == 63);

    b.set(64);
    CHECK(b.lowest_ge(64) == 64);
    CHECK(b.highest_le(64) == 64);
    CHECK(b.highest_le(63) == 63);
}

TEST_CASE("tick bitmap: queries past the end are clamped or empty") {
    TickBitmap b(4096);
    b.set(10);
    CHECK(b.lowest_ge(4096) == TickBitmap::kNone);
    CHECK(b.lowest_ge(100000) == TickBitmap::kNone);
    CHECK(b.highest_le(4096) == 10);  // clamped to the last bit
    CHECK(b.highest_le(100000) == 10);
}

TEST_CASE("tick bitmap: clear propagates upward only when a word empties") {
    TickBitmap b(4096);
    b.set(100);
    b.set(101);
    b.clear(100);
    CHECK_FALSE(b.test(100));
    CHECK(b.test(101));
    CHECK_FALSE(b.empty());
    b.clear(101);
    CHECK(b.empty());
    CHECK(b.lowest() == TickBitmap::kNone);
    CHECK(b.highest() == TickBitmap::kNone);
}

TEST_CASE("tick bitmap: clearing far-apart bits empties every level") {
    // Bits in different level-2 words, so all three levels must unwind.
    TickBitmap b(262144);
    b.set(0);
    b.set(150000);
    b.set(262143);
    CHECK(b.lowest() == 0);
    CHECK(b.highest() == 262143);
    b.clear(0);
    CHECK(b.lowest() == 150000);
    b.clear(262143);
    CHECK(b.highest() == 150000);
    b.clear(150000);
    CHECK(b.empty());
}

TEST_CASE("tick bitmap: crossing level-1 and level-2 boundaries") {
    TickBitmap b(262144);
    // 4096 is a level-1 word boundary (64 level-0 words); 262144/64 = 4096.
    for (std::uint32_t i : {4095U, 4096U, 4097U, 262143U}) {
        b.set(i);
    }
    CHECK(b.lowest_ge(0) == 4095);
    CHECK(b.lowest_ge(4096) == 4096);
    CHECK(b.lowest_ge(4098) == 262143);
    CHECK(b.highest_le(262143) == 262143);
    CHECK(b.highest_le(262142) == 4097);
    CHECK(b.highest_le(4096) == 4096);
    CHECK(b.highest_le(4094) == TickBitmap::kNone);
}

TEST_CASE("tick bitmap: sizes that are not a multiple of 64") {
    for (std::uint32_t n : {1U, 2U, 63U, 65U, 100U, 1000U, 4001U}) {
        TickBitmap b(n);
        CHECK(b.size() == n);
        CHECK(b.empty());
        b.set(n - 1);
        CHECK(b.highest() == n - 1);
        CHECK(b.lowest() == n - 1);
        CHECK(b.lowest_ge(n) == TickBitmap::kNone);
        b.clear(n - 1);
        CHECK(b.empty());
    }
}

TEST_CASE("tick bitmap: a single bit is found from every query point") {
    constexpr std::uint32_t kN = 1000;
    for (std::uint32_t at : {0U, 1U, 63U, 64U, 500U, 998U, 999U}) {
        TickBitmap b(kN);
        b.set(at);
        for (std::uint32_t q = 0; q < kN; ++q) {
            CHECK(b.lowest_ge(q) == (q <= at ? at : TickBitmap::kNone));
            CHECK(b.highest_le(q) == (q >= at ? at : TickBitmap::kNone));
        }
    }
}

TEST_CASE("tick bitmap: matches a naive reference over random churn") {
    constexpr std::uint32_t kN = 5000;
    TickBitmap b(kN);
    RefBitmap ref(kN);

    std::uint64_t state = 0xFEEDBEEF;
    auto rng = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };

    for (int step = 0; step < 60000; ++step) {
        std::uint64_t const r = rng();
        auto const i = static_cast<std::uint32_t>(r % kN);
        if ((r & 0x100U) != 0) {
            b.set(i);
            ref.set(i);
        } else {
            b.clear(i);
            ref.clear(i);
        }
        CHECK(b.test(i) == ref.test(i));

        if ((step & 0x3F) == 0) {
            REQUIRE(b.empty() == ref.empty());
            auto const q = static_cast<std::uint32_t>(rng() % kN);
            REQUIRE(b.lowest_ge(q) == ref.lowest_ge(q));
            REQUIRE(b.highest_le(q) == ref.highest_le(q));
            REQUIRE(b.lowest() == ref.lowest_ge(0));
            REQUIRE(b.highest() == ref.highest_le(kN - 1));
        }
    }
}

TEST_CASE("tick bitmap: dense fill then drain keeps queries exact") {
    constexpr std::uint32_t kN = 4096;
    TickBitmap b(kN);
    for (std::uint32_t i = 0; i < kN; ++i) {
        b.set(i);
    }
    CHECK(b.lowest() == 0);
    CHECK(b.highest() == kN - 1);
    // Drain from the bottom; the lowest must track exactly.
    for (std::uint32_t i = 0; i < kN; ++i) {
        CHECK(b.lowest() == i);
        b.clear(i);
    }
    CHECK(b.empty());

    // Drain from the top; the highest must track exactly.
    for (std::uint32_t i = 0; i < kN; ++i) {
        b.set(i);
    }
    for (std::uint32_t i = kN; i-- > 0;) {
        CHECK(b.highest() == i);
        b.clear(i);
    }
    CHECK(b.empty());
}
