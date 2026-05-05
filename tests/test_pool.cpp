#include <lazerbook/order_pool.hpp>

#include <doctest/doctest.h>
#include <vector>

using namespace lazerbook;

TEST_CASE("fresh pool reports full availability") {
    OrderPool pool(8);
    CHECK(pool.capacity() == 8);
    CHECK(pool.in_use() == 0);
    CHECK(pool.available() == 8);
}

TEST_CASE("acquire and release keep in_use accounting consistent") {
    OrderPool pool(4);
    Order* a = pool.acquire();
    Order* b = pool.acquire();
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a != b);
    CHECK(pool.in_use() == 2);
    CHECK(pool.available() == 2);
    pool.release(a);
    CHECK(pool.in_use() == 1);
    CHECK(pool.available() == 3);
    pool.release(b);
    CHECK(pool.in_use() == 0);
}

TEST_CASE("exhaustion returns nullptr") {
    OrderPool pool(3);
    std::vector<Order*> held;
    for (int i = 0; i < 3; ++i) {
        Order* o = pool.acquire();
        REQUIRE(o != nullptr);
        held.push_back(o);
    }
    CHECK(pool.acquire() == nullptr);
    CHECK(pool.available() == 0);
    pool.release(held.back());
    CHECK(pool.acquire() != nullptr);
}

TEST_CASE("released slots are reused (no unbounded growth)") {
    OrderPool pool(2);
    for (int i = 0; i < 1000; ++i) {
        Order* o = pool.acquire();
        REQUIRE(o != nullptr);
        o->shares = static_cast<std::uint32_t>(i);
        pool.release(o);
    }
    CHECK(pool.in_use() == 0);
    CHECK(pool.capacity() == 2);
}
