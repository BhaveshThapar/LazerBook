#include <lazerbook/order.hpp>
#include <lazerbook/order_index.hpp>
#include <lazerbook/types.hpp>

#include <cstdint>
#include <doctest/doctest.h>
#include <unordered_map>
#include <vector>

using namespace lazerbook;

namespace {

// The index stores pointers it never dereferences, so a stable dummy array is
// enough to give every id a distinguishable non-null value.
std::vector<Order>& dummies() {
    static std::vector<Order> v(1 << 16);
    return v;
}

Order* ptr_for(std::uint64_t id) { return &dummies()[id % dummies().size()]; }

}  // namespace

TEST_CASE("order index: empty lookups miss") {
    OrderIndex idx(64);
    CHECK(idx.size() == 0);
    CHECK(idx.empty());
    CHECK(idx.find(OrderId{1}) == nullptr);
    CHECK_FALSE(idx.contains(OrderId{1}));
    CHECK_FALSE(idx.erase(OrderId{1}));
}

TEST_CASE("order index: capacity rounds up to a power of two") {
    CHECK(OrderIndex(1).capacity() == 8);  // floored at 8
    CHECK(OrderIndex(8).capacity() == 8);
    CHECK(OrderIndex(9).capacity() == 16);
    CHECK(OrderIndex(1000).capacity() == 1024);
}

TEST_CASE("order index: insert then find round-trips") {
    OrderIndex idx(1024);
    for (std::uint64_t i = 1; i <= 100; ++i) {
        CHECK(idx.insert(OrderId{i}, ptr_for(i)));
    }
    CHECK(idx.size() == 100);
    for (std::uint64_t i = 1; i <= 100; ++i) {
        CHECK(idx.find(OrderId{i}) == ptr_for(i));
    }
    CHECK(idx.find(OrderId{101}) == nullptr);
}

TEST_CASE("order index: duplicate insert is rejected") {
    OrderIndex idx(64);
    CHECK(idx.insert(OrderId{7}, ptr_for(7)));
    CHECK_FALSE(idx.insert(OrderId{7}, ptr_for(8)));
    CHECK(idx.size() == 1);
    CHECK(idx.find(OrderId{7}) == ptr_for(7));
}

TEST_CASE("order index: erase removes and frees the slot") {
    OrderIndex idx(64);
    CHECK(idx.insert(OrderId{5}, ptr_for(5)));
    CHECK(idx.erase(OrderId{5}));
    CHECK(idx.size() == 0);
    CHECK(idx.find(OrderId{5}) == nullptr);
    CHECK_FALSE(idx.erase(OrderId{5}));
    // Slot is reusable.
    CHECK(idx.insert(OrderId{5}, ptr_for(6)));
    CHECK(idx.find(OrderId{5}) == ptr_for(6));
}

TEST_CASE("order index: a full table rejects rather than growing") {
    // Growing would allocate on the hot path, so exhaustion is a rejection.
    OrderIndex idx(8);
    std::size_t inserted = 0;
    for (std::uint64_t i = 1; i <= 32; ++i) {
        if (idx.insert(OrderId{i}, ptr_for(i))) {
            ++inserted;
        }
    }
    CHECK(inserted == 7);  // one slot stays free so probing terminates
    CHECK(idx.capacity() == 8);
    CHECK(idx.size() == 7);
}

TEST_CASE("order index: erase keeps later chain members findable") {
    // The backward-shift case that a naive "just clear the slot" gets wrong:
    // deleting an entry in the middle of a probe chain must not orphan the
    // entries behind it.
    OrderIndex idx(1024);
    constexpr std::uint64_t kN = 400;
    for (std::uint64_t i = 1; i <= kN; ++i) {
        REQUIRE(idx.insert(OrderId{i}, ptr_for(i)));
    }
    // Delete every third id, then everything else must still be findable.
    for (std::uint64_t i = 1; i <= kN; i += 3) {
        REQUIRE(idx.erase(OrderId{i}));
    }
    for (std::uint64_t i = 1; i <= kN; ++i) {
        if (i % 3 == 1) {
            CHECK(idx.find(OrderId{i}) == nullptr);
        } else {
            CHECK(idx.find(OrderId{i}) == ptr_for(i));
        }
    }
}

TEST_CASE("order index: clear empties without reallocating") {
    OrderIndex idx(64);
    for (std::uint64_t i = 1; i <= 20; ++i) {
        idx.insert(OrderId{i}, ptr_for(i));
    }
    std::size_t const cap = idx.capacity();
    idx.clear();
    CHECK(idx.size() == 0);
    CHECK(idx.capacity() == cap);
    CHECK(idx.find(OrderId{1}) == nullptr);
    CHECK(idx.insert(OrderId{1}, ptr_for(1)));
}

TEST_CASE("order index: sequential ids do not degrade into one long chain") {
    // ITCH order references are near-sequential, which clusters badly under
    // linear probing without a mixing step.
    OrderIndex idx(1 << 14);
    for (std::uint64_t i = 1'000'000; i < 1'008'000; ++i) {
        REQUIRE(idx.insert(OrderId{i}, ptr_for(i)));
    }
    for (std::uint64_t i = 1'000'000; i < 1'008'000; ++i) {
        CHECK(idx.find(OrderId{i}) == ptr_for(i));
    }
    CHECK(idx.size() == 8000);
}

TEST_CASE("order index: matches unordered_map over a random command stream") {
    // Differential against the container it replaces. Any divergence in probe
    // or deletion logic shows up as a lookup mismatch.
    OrderIndex idx(1 << 15);
    std::unordered_map<std::uint64_t, Order*> ref;

    std::uint64_t state = 0x9E3779B9;
    auto rng = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };

    std::vector<std::uint64_t> live;
    for (int step = 0; step < 200000; ++step) {
        std::uint64_t const r = rng();
        // Bias toward insert so the table fills, but keep it under load 0.5.
        bool const do_erase = (!live.empty()) && ((r & 3U) == 0 || live.size() > 12000);
        if (do_erase) {
            std::size_t const at = static_cast<std::size_t>(r >> 8) % live.size();
            std::uint64_t const id = live[at];
            live[at] = live.back();
            live.pop_back();
            CHECK(idx.erase(OrderId{id}) == (ref.erase(id) == 1));
        } else {
            std::uint64_t const id = (r >> 8) % 500000ULL;
            Order* p = ptr_for(id);
            bool const got = idx.insert(OrderId{id}, p);
            bool const want = ref.emplace(id, p).second;
            CHECK(got == want);
            if (want) {
                live.push_back(id);
            }
        }
        // Spot-check agreement continuously, not just at the end.
        if ((step & 0x3FF) == 0) {
            REQUIRE(idx.size() == ref.size());
            std::uint64_t const probe = rng() % 500000ULL;
            auto it = ref.find(probe);
            CHECK(idx.find(OrderId{probe}) == (it == ref.end() ? nullptr : it->second));
        }
    }

    REQUIRE(idx.size() == ref.size());
    for (auto const& [id, p] : ref) {
        CHECK(idx.find(OrderId{id}) == p);
    }
}

TEST_CASE("order index: cancelling in insertion order stays linear") {
    // Regression guard for a real O(n^2): a locality-preserving hash maps
    // near-sequential ids into one contiguous run, and backward-shift deletion
    // scans forward to the next free slot -- so erasing from the front of that
    // run walks the whole table each time. Measured at 36.9us p50 per cancel
    // on a 100k book before the hash was changed to scatter.
    //
    // Bounding total probe work rather than wall time keeps this meaningful
    // under sanitizers and on a loaded CI box.
    constexpr std::uint64_t kN = 60000;
    OrderIndex idx(kN * 2);
    for (std::uint64_t i = 1; i <= kN; ++i) {
        REQUIRE(idx.insert(OrderId{i}, ptr_for(i)));
    }
    REQUIRE(idx.size() == kN);

    // Longest contiguous occupied run bounds the cost of a single erase. With
    // a scattering hash at load factor 0.5 this stays in the tens; with a
    // sequential hash it would be the full 60000.
    CHECK(idx.longest_run() < 1000);

    for (std::uint64_t i = 1; i <= kN; ++i) {
        REQUIRE(idx.erase(OrderId{i}));
    }
    CHECK(idx.size() == 0);
}

TEST_CASE("order index: churn in place leaves no tombstone buildup") {
    // Repeatedly filling and draining must not degrade lookups, which is the
    // failure mode tombstoned deletion has.
    OrderIndex idx(256);
    for (int round = 0; round < 200; ++round) {
        for (std::uint64_t i = 1; i <= 100; ++i) {
            std::uint64_t const id = i + (static_cast<std::uint64_t>(round) * 1000);
            REQUIRE(idx.insert(OrderId{id}, ptr_for(id)));
        }
        for (std::uint64_t i = 1; i <= 100; ++i) {
            std::uint64_t const id = i + (static_cast<std::uint64_t>(round) * 1000);
            REQUIRE(idx.find(OrderId{id}) == ptr_for(id));
            REQUIRE(idx.erase(OrderId{id}));
        }
        REQUIRE(idx.size() == 0);
    }
}
