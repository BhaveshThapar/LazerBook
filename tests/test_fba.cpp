#include <lazerbook/book.hpp>
#include <lazerbook/events.hpp>
#include <lazerbook/fba.hpp>
#include <lazerbook/order_pool.hpp>

#include <cstdint>
#include <doctest/doctest.h>
#include <vector>

using namespace lazerbook;

namespace {

struct RecordingSink final : EventSink {
    std::vector<Fill> fills;
    void on_fill(Fill const& f) override { fills.push_back(f); }
    void on_accepted(OrderAccepted const&) override {}
    void on_cancelled(OrderCancelled const&) override {}
    void on_rejected(OrderRejected const&) override {}
};

}  // namespace

TEST_CASE("orders do not match intra-window") {
    Book book(Price4{100}, 100);
    OrderPool pool(64);
    RecordingSink sink;
    FbaMatcher fba(book, pool, sink);

    fba.on_new(OrderId{1}, Side::Sell, Price4{150}, 10);
    fba.on_new(OrderId{2}, Side::Buy, Price4{155}, 10);  // crosses, but no match yet
    CHECK(sink.fills.empty());
    CHECK(fba.resting_count() == 2);
}

TEST_CASE("clear runs a uniform-price auction") {
    Book book(Price4{100}, 100);
    OrderPool pool(64);
    RecordingSink sink;
    FbaMatcher fba(book, pool, sink);

    fba.on_new(OrderId{1}, Side::Buy, Price4{155}, 10);
    fba.on_new(OrderId{2}, Side::Sell, Price4{150}, 10);
    std::uint64_t const vol = fba.clear();
    CHECK(vol == 10);
    REQUIRE(sink.fills.size() == 1);
    // Clearing price is uniform and inside [150, 155].
    CHECK(value_of(sink.fills[0].price) >= 150);
    CHECK(value_of(sink.fills[0].price) <= 155);
    CHECK(fba.batch_number() == 1);
}

TEST_CASE("clear with no cross trades nothing") {
    Book book(Price4{100}, 100);
    OrderPool pool(64);
    RecordingSink sink;
    FbaMatcher fba(book, pool, sink);
    fba.on_new(OrderId{1}, Side::Buy, Price4{140}, 10);
    fba.on_new(OrderId{2}, Side::Sell, Price4{150}, 10);
    CHECK(fba.clear() == 0);
    CHECK(sink.fills.empty());
}

TEST_CASE("all crossing volume clears at one price") {
    Book book(Price4{100}, 100);
    OrderPool pool(64);
    RecordingSink sink;
    FbaMatcher fba(book, pool, sink);
    fba.on_new(OrderId{1}, Side::Buy, Price4{160}, 5);
    fba.on_new(OrderId{2}, Side::Buy, Price4{158}, 5);
    fba.on_new(OrderId{3}, Side::Sell, Price4{150}, 5);
    fba.on_new(OrderId{4}, Side::Sell, Price4{152}, 5);
    std::uint64_t const vol = fba.clear();
    CHECK(vol == 10);
    Price4 const px = sink.fills.front().price;
    for (auto const& f : sink.fills) {
        CHECK(f.price == px);  // single uniform price
    }
}

TEST_CASE("marginal-price fills are deterministic for a fixed seed") {
    auto run = [] {
        Book book(Price4{100}, 100);
        OrderPool pool(256);
        RecordingSink sink;
        FbaMatcher fba(book, pool, sink, 0xABCDEF);
        for (std::uint32_t i = 0; i < 20; ++i) {
            fba.on_new(OrderId{100 + i}, Side::Buy, Price4{150 + (i % 5)}, 3 + i);
            fba.on_new(OrderId{200 + i}, Side::Sell, Price4{148 + (i % 5)}, 2 + i);
        }
        fba.clear();
        return sink.fills;
    };
    auto a = run();
    auto b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].aggressor_id == b[i].aggressor_id);
        CHECK(a[i].passive_id == b[i].passive_id);
        CHECK(a[i].shares == b[i].shares);
        CHECK(a[i].price == b[i].price);
    }
}

TEST_CASE("Walrasian interval tie-break picks an interior price") {
    Book book(Price4{100}, 100);
    OrderPool pool(64);
    RecordingSink sink;
    FbaMatcher fba(book, pool, sink);
    // Bid at 160, ask at 150: any price in [150,160] clears 10. Midpoint = 155.
    fba.on_new(OrderId{1}, Side::Buy, Price4{160}, 10);
    fba.on_new(OrderId{2}, Side::Sell, Price4{150}, 10);
    fba.clear();
    REQUIRE(sink.fills.size() == 1);
    CHECK(value_of(sink.fills[0].price) == 155);
}

TEST_CASE("orders resting across multiple batches clear independently") {
    Book book(Price4{100}, 100);
    OrderPool pool(64);
    RecordingSink sink;
    FbaMatcher fba(book, pool, sink);

    // Batch 1: one crossing pair clears.
    fba.on_new(OrderId{1}, Side::Buy, Price4{155}, 10);
    fba.on_new(OrderId{2}, Side::Sell, Price4{150}, 10);
    CHECK(fba.clear() == 10);
    CHECK(fba.resting_count() == 0);

    // Batch 2: fresh orders, independent clear, batch number advanced.
    fba.on_new(OrderId{3}, Side::Buy, Price4{160}, 7);
    fba.on_new(OrderId{4}, Side::Sell, Price4{158}, 7);
    CHECK(fba.clear() == 7);
    CHECK(fba.batch_number() == 2);
    CHECK(sink.fills.size() == 2);
}

TEST_CASE("batch_number increments per clear") {
    Book book(Price4{100}, 100);
    OrderPool pool(64);
    RecordingSink sink;
    FbaMatcher fba(book, pool, sink);
    CHECK(fba.batch_number() == 0);
    fba.clear();
    fba.clear();
    CHECK(fba.batch_number() == 2);
}
