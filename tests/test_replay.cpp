#include <lazerbook/book.hpp>
#include <lazerbook/itch.hpp>
#include <lazerbook/itch_synth.hpp>
#include <lazerbook/order_pool.hpp>
#include <lazerbook/reconstruct.hpp>

#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <span>

using namespace lazerbook;

namespace {

std::uint64_t side_shares(Book const& book, Side side) {
    std::uint64_t total = 0;
    std::uint32_t const lo = value_of(book.min_price());
    for (std::uint32_t i = 0; i < book.num_ticks(); ++i) {
        if (PriceLevel const* lvl = book.level_at(Price4{lo + i}, side)) {
            total += lvl->total_shares;
        }
    }
    return total;
}

}  // namespace

TEST_CASE("50k-event synth -> parse -> reconstruct matches ground truth") {
    Price4 const min_price{50000};
    std::uint32_t const num_ticks = 512;
    itch::synth::Synth synth(0x5EED, min_price, num_ticks);
    Book book(min_price, num_ticks);
    OrderPool pool(1U << 17);
    Reconstructor recon(book, pool);

    std::array<std::uint8_t, 64> buf{};
    for (int i = 0; i < 50000; ++i) {
        std::size_t const n = synth.next(buf);
        REQUIRE(n > 0);
        // Exercise the real parser explicitly before applying.
        auto parsed = itch::parse(std::span<std::uint8_t const>(buf.data(), n));
        REQUIRE(parsed.has_value());
        recon.apply(*parsed);
    }

    auto const truth = synth.truth();
    CHECK(side_shares(book, Side::Buy) == truth.total_bid_shares);
    CHECK(side_shares(book, Side::Sell) == truth.total_ask_shares);
    CHECK(value_of(book.best_bid()) == truth.best_bid);
    CHECK(value_of(book.best_ask()) == truth.best_ask);
    CHECK(recon.live_order_count() == truth.bid_orders + truth.ask_orders);
    CHECK(synth.live_orders() == truth.bid_orders + truth.ask_orders);
}
