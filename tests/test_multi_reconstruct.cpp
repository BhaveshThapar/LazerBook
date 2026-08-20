#include <lazerbook/book_registry.hpp>
#include <lazerbook/multi_reconstruct.hpp>
#include <lazerbook/types.hpp>

#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <unordered_map>
#include <vector>

using namespace lazerbook;

namespace {

// Minimal ITCH encoder. The bundled synth hardcodes stock_locate to 1 and
// restarts order references from 1, so it cannot produce a multi-instrument
// stream with globally unique references -- which is exactly what routing has
// to be tested against.
struct Encoder {
    std::vector<std::uint8_t> buf;
    std::uint64_t ts = 0;

    void header(std::uint8_t* p, char type, std::uint16_t locate) {
        p[0] = static_cast<std::uint8_t>(type);
        write_be<std::uint16_t>(p + 1, locate);
        write_be<std::uint16_t>(p + 3, 0);
        write_be48(p + 5, ts++);
    }

    std::vector<std::uint8_t> add_order(
        std::uint16_t locate, std::uint64_t ref, Side side, std::uint32_t shares, Price4 price
    ) {
        std::vector<std::uint8_t> m(36, 0);
        header(m.data(), 'A', locate);
        write_be<std::uint64_t>(m.data() + 11, ref);
        m[19] = static_cast<std::uint8_t>(side);
        write_be<std::uint32_t>(m.data() + 20, shares);
        // symbol bytes 24..31 stay spaces; routing uses the locate.
        for (int i = 24; i < 32; ++i) {
            m[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(' ');
        }
        write_be<Price4>(m.data() + 32, price);
        return m;
    }

    std::vector<std::uint8_t> order_delete(std::uint16_t locate, std::uint64_t ref) {
        std::vector<std::uint8_t> m(19, 0);
        header(m.data(), 'D', locate);
        write_be<std::uint64_t>(m.data() + 11, ref);
        return m;
    }

    std::vector<std::uint8_t> order_executed(
        std::uint16_t locate, std::uint64_t ref, std::uint32_t shares
    ) {
        std::vector<std::uint8_t> m(31, 0);
        header(m.data(), 'E', locate);
        write_be<std::uint64_t>(m.data() + 11, ref);
        write_be<std::uint32_t>(m.data() + 19, shares);
        write_be<std::uint64_t>(m.data() + 23, 0);
        return m;
    }

    std::vector<std::uint8_t> order_cancel(
        std::uint16_t locate, std::uint64_t ref, std::uint32_t shares
    ) {
        std::vector<std::uint8_t> m(23, 0);
        header(m.data(), 'X', locate);
        write_be<std::uint64_t>(m.data() + 11, ref);
        write_be<std::uint32_t>(m.data() + 19, shares);
        return m;
    }
};

// Per-instrument expected state, tracked independently of the engine.
struct Truth {
    std::uint64_t bid_shares = 0;
    std::uint64_t ask_shares = 0;
};

struct LiveOrder {
    std::uint16_t locate;
    Side side;
    std::uint32_t shares;
};

}  // namespace

TEST_CASE("book registry: creates lazily and only for instruments seen") {
    BookRegistry reg(100);
    CHECK(reg.book_count() == 0);
    CHECK(reg.find(5) == nullptr);

    Book* b = reg.get_or_create(5, Price4{150000});
    REQUIRE(b != nullptr);
    CHECK(reg.book_count() == 1);
    CHECK(reg.find(5) == b);
    // Asking again returns the same book, not a new one.
    CHECK(reg.get_or_create(5, Price4{999999}) == b);
    CHECK(reg.book_count() == 1);
    CHECK(reg.find(6) == nullptr);
}

TEST_CASE("book registry: locate beyond capacity is refused, not undefined") {
    BookRegistry reg(10);
    CHECK(reg.get_or_create(11, Price4{150000}) == nullptr);
    CHECK(reg.find(11) == nullptr);
    CHECK(reg.book_count() == 0);
}

TEST_CASE("book registry: window is centred on the seeding price") {
    BookRegistry reg(10);
    Book* b = reg.get_or_create(1, Price4{500000});  // $50.00
    REQUIRE(b != nullptr);
    CHECK(b->in_range(Price4{500000}));
    // Penny grid for a $50 stock.
    CHECK(b->tick_size() == 100);
    // Default window is 40% of price, so +/-20% around $50 is +/-$10.
    CHECK(b->in_range(Price4{500000 - 90000}));
    CHECK(b->in_range(Price4{500000 + 90000}));
    CHECK_FALSE(b->in_range(Price4{500000 - 150000}));
    CHECK_FALSE(b->in_range(Price4{500000 + 150000}));
}

TEST_CASE("book registry: window width scales with the instrument's price") {
    // A fixed tick count is either too narrow for a $400 stock or ruinously
    // wasteful for a $3 one. These must not come out the same size.
    BookRegistry reg(10);
    Book const* cheap = reg.get_or_create(1, Price4{30000});   // $3
    Book const* mid = reg.get_or_create(2, Price4{500000});    // $50
    Book const* dear = reg.get_or_create(3, Price4{4000000});  // $400
    REQUIRE(cheap != nullptr);
    REQUIRE(mid != nullptr);
    REQUIRE(dear != nullptr);

    CHECK(cheap->num_ticks() < mid->num_ticks());
    CHECK(mid->num_ticks() < dear->num_ticks());
    // Each still spans a band proportional to its own level.
    CHECK(cheap->in_range(Price4{30000 + 5000}));  // +$0.50 on a $3 name
    // $400 * 40% = $160 of width, which exceeds max_ticks (8192 penny slots =
    // $81.92), so the clamp caps the band at about +/-$41.
    CHECK(dear->in_range(Price4{4000000 + 350000}));  // +$35 on a $400 name
}

TEST_CASE("book registry: clamps keep tiny and huge prices bounded") {
    BookRegistry::Config cfg;
    cfg.min_ticks = 256;
    cfg.max_ticks = 1024;
    BookRegistry reg(10, cfg);
    // A penny stock would compute far below min_ticks.
    Book const* tiny = reg.get_or_create(1, Price4{10000});  // $1.00
    REQUIRE(tiny != nullptr);
    CHECK(tiny->num_ticks() == 256);
    // A very expensive name would compute far above max_ticks.
    Book const* huge = reg.get_or_create(2, Price4{50000000});  // $5000
    REQUIRE(huge != nullptr);
    CHECK(huge->num_ticks() == 1024);
}

TEST_CASE("book registry: sub-dollar instruments keep sub-penny ticks") {
    BookRegistry reg(10);
    Book* b = reg.get_or_create(1, Price4{5000});  // $0.50
    REQUIRE(b != nullptr);
    CHECK(b->tick_size() == 1);
    CHECK(b->in_range(Price4{5001}));  // sub-penny increment has a slot
}

TEST_CASE("book registry: off-grid prices are out of range, never rounded") {
    // Rounding an off-grid price into a neighbouring slot would silently
    // corrupt the reconstruction, so it must be reported instead.
    BookRegistry reg(10);
    Book* b = reg.get_or_create(1, Price4{500000});
    REQUIRE(b != nullptr);
    REQUIRE(b->tick_size() == 100);
    CHECK(b->in_range(Price4{500000}));
    CHECK_FALSE(b->in_range(Price4{500001}));
    CHECK_FALSE(b->in_range(Price4{500099}));
    CHECK(b->in_range(Price4{500100}));
}

TEST_CASE("multi reconstruct: routes messages to the right instrument") {
    MultiReconstructor r(16, 1024);
    Encoder enc;

    // Same price on two instruments; each must land in its own book.
    auto a1 = enc.add_order(1, 100, Side::Buy, 500, Price4{200000});
    auto a2 = enc.add_order(2, 200, Side::Buy, 700, Price4{200000});
    CHECK(r.apply(a1) == a1.size());
    CHECK(r.apply(a2) == a2.size());

    CHECK(r.stats().added == 2);
    CHECK(r.registry().book_count() == 2);

    Book const* b1 = r.registry().find(1);
    Book const* b2 = r.registry().find(2);
    REQUIRE(b1 != nullptr);
    REQUIRE(b2 != nullptr);
    CHECK(b1->level_at(Price4{200000}, Side::Buy)->total_shares == 500);
    CHECK(b2->level_at(Price4{200000}, Side::Buy)->total_shares == 700);
    CHECK(r.total_shares(Side::Buy) == 1200);
}

TEST_CASE("multi reconstruct: order-only messages find their instrument by reference") {
    // E/C/X/D name no symbol. The engine has to recover the owning book from
    // the reference alone.
    MultiReconstructor r(16, 1024);
    Encoder enc;

    auto a1 = enc.add_order(3, 100, Side::Sell, 500, Price4{300000});
    auto a2 = enc.add_order(7, 200, Side::Sell, 900, Price4{300000});
    r.apply(a1);
    r.apply(a2);

    // Delete carries locate 0 -- deliberately wrong -- so a correct
    // implementation must ignore it and use the reference.
    auto d = enc.order_delete(0, 100);
    CHECK(r.apply(d) == d.size());

    CHECK(r.registry().find(3)->level_at(Price4{300000}, Side::Sell)->total_shares == 0);
    CHECK(r.registry().find(7)->level_at(Price4{300000}, Side::Sell)->total_shares == 900);
    CHECK(r.total_shares(Side::Sell) == 900);
    CHECK(r.live_order_count() == 1);
}

TEST_CASE("multi reconstruct: unknown references are counted, not fatal") {
    MultiReconstructor r(16, 1024);
    Encoder enc;
    auto d = enc.order_delete(1, 999);
    r.apply(d);
    auto e = enc.order_executed(1, 999, 10);
    r.apply(e);
    CHECK(r.stats().skip_unknown_ref == 2);
    CHECK(r.live_order_count() == 0);
}

TEST_CASE("multi reconstruct: a locate past the registry is counted") {
    MultiReconstructor r(4, 1024);
    Encoder enc;
    auto a = enc.add_order(99, 1, Side::Buy, 100, Price4{200000});
    r.apply(a);
    CHECK(r.stats().skip_no_book == 1);
    CHECK(r.live_order_count() == 0);
}

TEST_CASE("multi reconstruct: prices outside an instrument's window are counted") {
    MultiReconstructor r(16, 1024);
    Encoder enc;
    auto seed = enc.add_order(1, 1, Side::Buy, 100, Price4{200000});
    r.apply(seed);
    // Far outside the +/- half-window around $20.
    auto far = enc.add_order(1, 2, Side::Buy, 100, Price4{9000000});
    r.apply(far);
    CHECK(r.stats().skip_oor == 1);
    CHECK(r.live_order_count() == 1);
}

TEST_CASE("multi reconstruct: matches independently tracked truth across instruments") {
    constexpr std::uint16_t kInstruments = 400;
    MultiReconstructor r(kInstruments, 1 << 16);
    Encoder enc;

    std::unordered_map<std::uint64_t, LiveOrder> live;
    std::vector<std::uint64_t> live_refs;
    std::vector<Truth> truth(kInstruments + 1);

    std::uint64_t state = 0x51E7;
    auto rng = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };

    std::uint64_t next_ref = 1;
    for (int step = 0; step < 200000; ++step) {
        std::uint64_t const q = rng();
        bool const do_add = live_refs.empty() || (q % 100) < 55;

        if (do_add) {
            // Fresh draw: deriving the locate from the same word as the
            // add/remove decision correlates them, because q % 400 determines
            // q % 100. That silently confined adds to 221 of the 400
            // instruments.
            auto const locate = static_cast<std::uint16_t>(1 + (rng() % kInstruments));
            Side const side = ((q >> 8) & 1U) ? Side::Buy : Side::Sell;
            auto const shares = static_cast<std::uint32_t>(1 + ((q >> 16) % 500));
            // Each instrument sits at its own level; prices stay on the penny
            // grid and well inside the window the first message establishes.
            std::uint32_t const base = 100000U + (static_cast<std::uint32_t>(locate) * 10000U);
            std::uint32_t const price = base + (static_cast<std::uint32_t>((q >> 24) % 200) * 100U);

            std::uint64_t const ref = next_ref++;
            auto msg = enc.add_order(locate, ref, side, shares, Price4{price});
            REQUIRE(r.apply(msg) == msg.size());

            live[ref] = LiveOrder{locate, side, shares};
            live_refs.push_back(ref);
            (side == Side::Buy ? truth[locate].bid_shares : truth[locate].ask_shares) += shares;
        } else {
            std::size_t const at = static_cast<std::size_t>(q >> 8) % live_refs.size();
            std::uint64_t const ref = live_refs[at];
            LiveOrder& lo = live[ref];
            std::uint64_t& bucket =
                (lo.side == Side::Buy) ? truth[lo.locate].bid_shares : truth[lo.locate].ask_shares;

            std::uint64_t const kind = (q >> 32) % 3;
            if (kind == 0) {
                // Full delete.
                auto msg = enc.order_delete(0, ref);
                REQUIRE(r.apply(msg) == msg.size());
                bucket -= lo.shares;
                live_refs[at] = live_refs.back();
                live_refs.pop_back();
                live.erase(ref);
            } else {
                // Partial execute or cancel; may fully consume the order.
                auto const qty = static_cast<std::uint32_t>(1 + ((q >> 40) % 600));
                std::uint32_t const applied = (qty >= lo.shares) ? lo.shares : qty;
                auto msg =
                    (kind == 1) ? enc.order_executed(0, ref, qty) : enc.order_cancel(0, ref, qty);
                REQUIRE(r.apply(msg) == msg.size());
                bucket -= applied;
                lo.shares -= applied;
                if (lo.shares == 0) {
                    live_refs[at] = live_refs.back();
                    live_refs.pop_back();
                    live.erase(ref);
                }
            }
        }
    }

    // Aggregate agreement.
    std::uint64_t want_bid = 0;
    std::uint64_t want_ask = 0;
    for (auto const& t : truth) {
        want_bid += t.bid_shares;
        want_ask += t.ask_shares;
    }
    CHECK(r.total_shares(Side::Buy) == want_bid);
    CHECK(r.total_shares(Side::Sell) == want_ask);
    CHECK(r.live_order_count() == live.size());
    CHECK(r.pool_in_use() == live.size());
    CHECK(r.stats().skip_oor == 0);
    CHECK(r.stats().skip_no_book == 0);
    CHECK(r.stats().skip_unknown_ref == 0);
    CHECK(r.registry().book_count() == kInstruments);

    // Per-instrument agreement -- the aggregate could match while individual
    // books are cross-contaminated.
    for (std::uint16_t locate = 1; locate <= kInstruments; ++locate) {
        Book const* b = r.registry().find(locate);
        REQUIRE(b != nullptr);
        std::uint64_t bid = 0;
        std::uint64_t ask = 0;
        for (std::uint32_t t = 0; t < b->num_ticks(); ++t) {
            bid += b->level_by_index(t, Side::Buy).total_shares;
            ask += b->level_by_index(t, Side::Sell).total_shares;
        }
        REQUIRE(bid == truth[locate].bid_shares);
        REQUIRE(ask == truth[locate].ask_shares);
    }
}
