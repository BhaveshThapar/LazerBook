#include <lazerbook/book.hpp>
#include <lazerbook/order_pool.hpp>

#include <doctest/doctest.h>

using namespace lazerbook;

namespace {

Order* make(OrderPool& pool, OrderId id, Side side, std::uint32_t px, std::uint32_t shares) {
    Order* o = pool.acquire();
    REQUIRE(o != nullptr);
    o->id = id;
    o->side = side;
    o->price = Price4{px};
    o->shares = shares;
    return o;
}

}  // namespace

TEST_CASE("empty book reports zero best prices") {
    Book book(Price4{100}, 100);
    CHECK(value_of(book.best_bid()) == 0);
    CHECK(value_of(book.best_ask()) == 0);
    CHECK(book.empty(Side::Buy));
    CHECK(book.empty(Side::Sell));
}

TEST_CASE("best bid tracks the highest priced bid") {
    Book book(Price4{100}, 100);
    OrderPool pool(16);
    book.add(make(pool, OrderId{1}, Side::Buy, 150, 10));
    CHECK(value_of(book.best_bid()) == 150);
    book.add(make(pool, OrderId{2}, Side::Buy, 155, 10));
    CHECK(value_of(book.best_bid()) == 155);
    book.add(make(pool, OrderId{3}, Side::Buy, 152, 10));
    CHECK(value_of(book.best_bid()) == 155);  // unchanged
}

TEST_CASE("best ask tracks the lowest priced ask") {
    Book book(Price4{100}, 100);
    OrderPool pool(16);
    book.add(make(pool, OrderId{1}, Side::Sell, 160, 10));
    CHECK(value_of(book.best_ask()) == 160);
    book.add(make(pool, OrderId{2}, Side::Sell, 158, 10));
    CHECK(value_of(book.best_ask()) == 158);
    book.add(make(pool, OrderId{3}, Side::Sell, 165, 10));
    CHECK(value_of(book.best_ask()) == 158);
}

TEST_CASE("removing the best level refreshes the cursor downward/upward") {
    Book book(Price4{100}, 100);
    OrderPool pool(16);
    Order* top_bid = make(pool, OrderId{1}, Side::Buy, 155, 10);
    book.add(top_bid);
    book.add(make(pool, OrderId{2}, Side::Buy, 150, 10));
    book.remove(top_bid);
    CHECK(value_of(book.best_bid()) == 150);

    Order* low_ask = make(pool, OrderId{3}, Side::Sell, 160, 10);
    book.add(low_ask);
    book.add(make(pool, OrderId{4}, Side::Sell, 165, 10));
    book.remove(low_ask);
    CHECK(value_of(book.best_ask()) == 165);
}

TEST_CASE("emptying every level resets the side to empty") {
    Book book(Price4{100}, 100);
    OrderPool pool(16);
    Order* a = make(pool, OrderId{1}, Side::Buy, 150, 10);
    book.add(a);
    book.remove(a);
    CHECK(book.empty(Side::Buy));
    CHECK(value_of(book.best_bid()) == 0);
}

TEST_CASE("level aggregates total_shares and order_count") {
    Book book(Price4{100}, 100);
    OrderPool pool(16);
    book.add(make(pool, OrderId{1}, Side::Buy, 150, 10));
    book.add(make(pool, OrderId{2}, Side::Buy, 150, 25));
    PriceLevel const* lvl = book.level_at(Price4{150}, Side::Buy);
    REQUIRE(lvl != nullptr);
    CHECK(lvl->order_count == 2);
    CHECK(lvl->total_shares == 35);
}

TEST_CASE("time priority: head is the earliest order at a level") {
    Book book(Price4{100}, 100);
    OrderPool pool(16);
    Order* first = make(pool, OrderId{1}, Side::Buy, 150, 10);
    Order* second = make(pool, OrderId{2}, Side::Buy, 150, 20);
    book.add(first);
    book.add(second);
    PriceLevel const* lvl = book.level_at(Price4{150}, Side::Buy);
    REQUIRE(lvl != nullptr);
    CHECK(lvl->head == first);
    CHECK(lvl->tail == second);
}

TEST_CASE("reduce decrements shares and removes at zero") {
    Book book(Price4{100}, 100);
    OrderPool pool(16);
    Order* o = make(pool, OrderId{1}, Side::Buy, 150, 100);
    book.add(o);
    book.reduce(o, 40);
    CHECK(o->shares == 60);
    PriceLevel const* lvl = book.level_at(Price4{150}, Side::Buy);
    REQUIRE(lvl != nullptr);
    CHECK(lvl->total_shares == 60);
    book.reduce(o, 60);  // to zero -> unlinked
    CHECK(book.empty(Side::Buy));
}

TEST_CASE("successive reduces decrement then unlink at zero") {
    Book book(Price4{100}, 100);
    OrderPool pool(16);
    Order* o = make(pool, OrderId{1}, Side::Sell, 160, 90);
    book.add(o);
    book.reduce(o, 30);
    CHECK(o->shares == 60);
    book.reduce(o, 30);
    CHECK(o->shares == 30);
    PriceLevel const* lvl = book.level_at(Price4{160}, Side::Sell);
    REQUIRE(lvl != nullptr);
    CHECK(lvl->total_shares == 30);
    book.reduce(o, 30);  // exactly to zero -> unlinked, side empties
    CHECK(book.empty(Side::Sell));
    CHECK(value_of(book.best_ask()) == 0);
}

TEST_CASE("in_range guards the price window") {
    Book book(Price4{100}, 50);  // 100..149
    CHECK(book.in_range(Price4{100}));
    CHECK(book.in_range(Price4{149}));
    CHECK_FALSE(book.in_range(Price4{150}));
    CHECK_FALSE(book.in_range(Price4{99}));
    CHECK(book.level_at(Price4{200}, Side::Buy) == nullptr);
}
