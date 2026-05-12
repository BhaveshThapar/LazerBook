#include <lazerbook/book.hpp>
#include <lazerbook/events.hpp>
#include <lazerbook/matcher.hpp>
#include <lazerbook/order_pool.hpp>

#include <cstdint>
#include <doctest/doctest.h>
#include <vector>

using namespace lazerbook;

namespace {

struct RecordingSink final : EventSink {
    std::vector<Fill> fills;
    std::vector<OrderAccepted> accepts;
    std::vector<OrderCancelled> cancels;
    std::vector<OrderRejected> rejects;

    void on_fill(Fill const& f) override { fills.push_back(f); }
    void on_accepted(OrderAccepted const& a) override { accepts.push_back(a); }
    void on_cancelled(OrderCancelled const& c) override { cancels.push_back(c); }
    void on_rejected(OrderRejected const& r) override { rejects.push_back(r); }
};

struct Fixture {
    Book book{Price4{100}, 200};  // prices 100..299
    OrderPool pool{1024};
    RecordingSink sink;
    Matcher m{book, pool, sink};
};

}  // namespace

TEST_CASE("limit order with no cross rests on the book") {
    Fixture f;
    f.m.on_new(OrderId{1}, Side::Buy, OrderType::Limit, Price4{150}, 10);
    CHECK(f.sink.fills.empty());
    REQUIRE(f.sink.accepts.size() == 1);
    CHECK(f.sink.accepts[0].shares == 10);
    CHECK(f.m.resting_count() == 1);
}

TEST_CASE("crossing limit fills at the passive price") {
    Fixture f;
    f.m.on_new(OrderId{1}, Side::Sell, OrderType::Limit, Price4{150}, 10);
    f.m.on_new(OrderId{2}, Side::Buy, OrderType::Limit, Price4{155}, 10);
    REQUIRE(f.sink.fills.size() == 1);
    CHECK(f.sink.fills[0].price == Price4{150});  // passive price, not 155
    CHECK(f.sink.fills[0].shares == 10);
    CHECK(f.sink.fills[0].aggressor_side == Side::Buy);
    CHECK(f.m.resting_count() == 0);
}

TEST_CASE("partial fill rests the remainder") {
    Fixture f;
    f.m.on_new(OrderId{1}, Side::Sell, OrderType::Limit, Price4{150}, 10);
    f.m.on_new(OrderId{2}, Side::Buy, OrderType::Limit, Price4{150}, 15);
    REQUIRE(f.sink.fills.size() == 1);
    CHECK(f.sink.fills[0].shares == 10);
    REQUIRE(f.sink.accepts.size() == 2);
    CHECK(f.sink.accepts.back().id == OrderId{2});
    CHECK(f.sink.accepts.back().shares == 5);  // post-match remainder
    CHECK(value_of(f.book.best_bid()) == 150);
}

TEST_CASE("limit sweeps multiple price levels in price-time order") {
    Fixture f;
    f.m.on_new(OrderId{1}, Side::Sell, OrderType::Limit, Price4{150}, 5);
    f.m.on_new(OrderId{2}, Side::Sell, OrderType::Limit, Price4{151}, 5);
    f.m.on_new(OrderId{3}, Side::Sell, OrderType::Limit, Price4{152}, 5);
    f.m.on_new(OrderId{4}, Side::Buy, OrderType::Limit, Price4{152}, 12);
    REQUIRE(f.sink.fills.size() == 3);
    CHECK(f.sink.fills[0].price == Price4{150});
    CHECK(f.sink.fills[0].shares == 5);
    CHECK(f.sink.fills[1].price == Price4{151});
    CHECK(f.sink.fills[2].price == Price4{152});
    CHECK(f.sink.fills[2].shares == 2);
    CHECK(f.m.resting_count() == 1);  // the 152 sell has 3 left
}

TEST_CASE("market order takes all liquidity and never rests") {
    Fixture f;
    f.m.on_new(OrderId{1}, Side::Sell, OrderType::Limit, Price4{150}, 5);
    f.m.on_new(OrderId{2}, Side::Sell, OrderType::Limit, Price4{151}, 5);
    f.m.on_new(OrderId{3}, Side::Buy, OrderType::Market, Price4{0}, 100);
    CHECK(f.sink.fills.size() == 2);
    CHECK(f.m.resting_count() == 0);  // remainder not rested
}

TEST_CASE("IOC fills what crosses and discards the rest") {
    Fixture f;
    f.m.on_new(OrderId{1}, Side::Sell, OrderType::Limit, Price4{150}, 5);
    f.m.on_new(OrderId{2}, Side::Buy, OrderType::Ioc, Price4{150}, 10);
    REQUIRE(f.sink.fills.size() == 1);
    CHECK(f.sink.fills[0].shares == 5);
    CHECK(f.m.resting_count() == 0);
    // Only the resting sell was accepted; the IOC itself never is.
    REQUIRE(f.sink.accepts.size() == 1);
    CHECK(f.sink.accepts[0].id == OrderId{1});
}

TEST_CASE("FOK fully fills when liquidity suffices") {
    Fixture f;
    f.m.on_new(OrderId{1}, Side::Sell, OrderType::Limit, Price4{150}, 6);
    f.m.on_new(OrderId{2}, Side::Sell, OrderType::Limit, Price4{151}, 6);
    f.m.on_new(OrderId{3}, Side::Buy, OrderType::Fok, Price4{151}, 10);
    CHECK(f.sink.rejects.empty());
    std::uint32_t total = 0;
    for (auto const& fill : f.sink.fills) {
        total += fill.shares;
    }
    CHECK(total == 10);
}

TEST_CASE("FOK rejects and does not partially fill when liquidity is short") {
    Fixture f;
    f.m.on_new(OrderId{1}, Side::Sell, OrderType::Limit, Price4{150}, 5);
    f.m.on_new(OrderId{2}, Side::Buy, OrderType::Fok, Price4{150}, 10);
    CHECK(f.sink.fills.empty());
    REQUIRE(f.sink.rejects.size() == 1);
    CHECK(f.sink.rejects[0].reason == RejectReason::FokWouldNotFullyFill);
    CHECK(value_of(f.book.best_ask()) == 150);  // resting liquidity untouched
}

TEST_CASE("cancel emits remaining and frees the slot") {
    Fixture f;
    f.m.on_new(OrderId{1}, Side::Buy, OrderType::Limit, Price4{150}, 40);
    f.m.on_cancel(OrderId{1});
    REQUIRE(f.sink.cancels.size() == 1);
    CHECK(f.sink.cancels[0].remaining == 40);
    CHECK(f.m.resting_count() == 0);
    CHECK(f.pool.in_use() == 0);
}

TEST_CASE("cancel of an unknown id is rejected") {
    Fixture f;
    f.m.on_cancel(OrderId{999});
    REQUIRE(f.sink.rejects.size() == 1);
    CHECK(f.sink.rejects[0].reason == RejectReason::OrderNotFound);
}

TEST_CASE("modify is cancel-replace and loses time priority") {
    Fixture f;
    f.m.on_new(OrderId{1}, Side::Buy, OrderType::Limit, Price4{150}, 10);
    f.m.on_modify(OrderId{1}, OrderId{2}, Price4{151}, 20);
    REQUIRE(f.sink.cancels.size() == 1);
    CHECK(f.sink.cancels[0].id == OrderId{1});
    REQUIRE(f.sink.accepts.size() == 2);
    CHECK(f.sink.accepts.back().id == OrderId{2});
    CHECK(value_of(f.book.best_bid()) == 151);
}

TEST_CASE("modify of an unknown id is rejected") {
    Fixture f;
    f.m.on_modify(OrderId{404}, OrderId{405}, Price4{150}, 10);
    REQUIRE(f.sink.rejects.size() == 1);
    CHECK(f.sink.rejects[0].reason == RejectReason::OrderNotFound);
    CHECK(f.m.resting_count() == 0);
}

TEST_CASE("duplicate order id is rejected") {
    Fixture f;
    f.m.on_new(OrderId{1}, Side::Buy, OrderType::Limit, Price4{150}, 10);
    f.m.on_new(OrderId{1}, Side::Buy, OrderType::Limit, Price4{151}, 10);
    REQUIRE(f.sink.rejects.size() == 1);
    CHECK(f.sink.rejects[0].reason == RejectReason::DuplicateOrderId);
}

TEST_CASE("out-of-range price is rejected") {
    Fixture f;
    f.m.on_new(OrderId{1}, Side::Buy, OrderType::Limit, Price4{500}, 10);
    REQUIRE(f.sink.rejects.size() == 1);
    CHECK(f.sink.rejects[0].reason == RejectReason::PriceOutOfRange);
}

TEST_CASE("zero shares is rejected") {
    Fixture f;
    f.m.on_new(OrderId{1}, Side::Buy, OrderType::Limit, Price4{150}, 0);
    REQUIRE(f.sink.rejects.size() == 1);
    CHECK(f.sink.rejects[0].reason == RejectReason::InvalidShares);
}

TEST_CASE("pool exhaustion is reported") {
    Book book(Price4{100}, 200);
    OrderPool pool(2);
    RecordingSink sink;
    Matcher m(book, pool, sink);
    m.on_new(OrderId{1}, Side::Buy, OrderType::Limit, Price4{150}, 10);
    m.on_new(OrderId{2}, Side::Buy, OrderType::Limit, Price4{149}, 10);
    m.on_new(OrderId{3}, Side::Buy, OrderType::Limit, Price4{148}, 10);
    REQUIRE_FALSE(sink.rejects.empty());
    CHECK(sink.rejects.back().reason == RejectReason::PoolExhausted);
}

TEST_CASE("100k random commands leave the pool leak-free") {
    Book book(Price4{1000}, 200);  // 1000..1199
    OrderPool pool(4096);
    RecordingSink sink;
    Matcher m(book, pool, sink);

    std::uint64_t state = 0x1234567ULL;
    auto rng = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };

    std::vector<OrderId> live;
    for (int i = 0; i < 100000; ++i) {
        std::uint64_t const r = rng();
        if ((r & 3U) == 0 && !live.empty()) {
            OrderId const id = live[r % live.size()];
            m.on_cancel(id);
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(r % live.size()));
        } else {
            auto const id = OrderId{static_cast<std::uint64_t>(i) + 1};
            Side const side = (r & 4U) ? Side::Buy : Side::Sell;
            auto const px = Price4{1000U + static_cast<std::uint32_t>(r % 200U)};
            auto const sh = 1U + static_cast<std::uint32_t>((r >> 8) % 100U);
            auto const ty = (r & 8U) ? OrderType::Limit : OrderType::Ioc;
            m.on_new(id, side, ty, px, sh);
            if (ty == OrderType::Limit) {
                live.push_back(id);
            }
        }
    }
    CHECK(pool.in_use() == m.resting_count());
}
