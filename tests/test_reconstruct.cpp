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

std::uint64_t book_side_shares(Book const& book, Side side) {
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

TEST_CASE("reconstructed book matches synth ground truth") {
    Price4 const min_price{10000};
    std::uint32_t const num_ticks = 256;
    itch::synth::Synth synth(0xC0FFEE, min_price, num_ticks);
    Book book(min_price, num_ticks);
    OrderPool pool(1U << 16);
    Reconstructor recon(book, pool);

    std::array<std::uint8_t, 64> buf{};
    for (int i = 0; i < 20000; ++i) {
        std::size_t const n = synth.next(buf);
        REQUIRE(n > 0);
        recon.apply_bytes(std::span<std::uint8_t const>(buf.data(), n));
    }

    auto const truth = synth.truth();
    CHECK(book_side_shares(book, Side::Buy) == truth.total_bid_shares);
    CHECK(book_side_shares(book, Side::Sell) == truth.total_ask_shares);
    CHECK(value_of(book.best_bid()) == truth.best_bid);
    CHECK(value_of(book.best_ask()) == truth.best_ask);
    CHECK(recon.live_order_count() == truth.bid_orders + truth.ask_orders);
}

TEST_CASE("OrderReplace removes the original and adds the replacement") {
    Book book(Price4{100}, 100);
    OrderPool pool(64);
    Reconstructor recon(book, pool);

    itch::AddOrder add;
    add.order_reference_number = OrderId{1};
    add.buy_sell_indicator = Side::Buy;
    add.shares = 10;
    add.price = Price4{150};
    recon.apply(add);
    CHECK(recon.live_order_count() == 1);

    itch::OrderReplace rep;
    rep.original_order_reference_number = OrderId{1};
    rep.new_order_reference_number = OrderId{2};
    rep.shares = 20;
    rep.price = Price4{152};
    recon.apply(rep);
    CHECK(recon.live_order_count() == 1);
    CHECK(value_of(book.best_bid()) == 152);
    CHECK(book_side_shares(book, Side::Buy) == 20);
}

TEST_CASE("skip counters increment on bad input") {
    Book book(Price4{100}, 100);
    OrderPool pool(64);
    Reconstructor recon(book, pool);

    itch::OrderExecuted exec;  // unknown ref
    exec.order_reference_number = OrderId{42};
    exec.executed_shares = 5;
    recon.apply(exec);
    CHECK(recon.stats().skip_unknown_ref == 1);

    itch::AddOrder oor;  // price out of range
    oor.order_reference_number = OrderId{7};
    oor.buy_sell_indicator = Side::Sell;
    oor.shares = 5;
    oor.price = Price4{9999};
    recon.apply(oor);
    CHECK(recon.stats().skip_oor == 1);

    itch::Unknown unk;  // unhandled type
    recon.apply(unk);
    CHECK(recon.stats().skip_unhandled == 1);
}

TEST_CASE("apply_bytes counts parse errors") {
    Book book(Price4{100}, 100);
    OrderPool pool(64);
    Reconstructor recon(book, pool);
    std::array<std::uint8_t, 4> junk{'A', 0, 0, 0};
    recon.apply_bytes(std::span<std::uint8_t const>(junk.data(), junk.size()));
    CHECK(recon.stats().skip_parse_error == 1);
}
