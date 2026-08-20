#include <lazerbook/book.hpp>
#include <lazerbook/events.hpp>
#include <lazerbook/matcher.hpp>
#include <lazerbook/order_index.hpp>
#include <lazerbook/order_pool.hpp>
#include <lazerbook/reconstruct.hpp>

#include "alloc_counter.hpp"

#include <cstdint>
#include <doctest/doctest.h>

// The resume-level claim is "zero hot-path allocation". These tests assert it
// by counting global operator new calls across the region, so a regression --
// a stray std::vector, a container that rehashes -- fails the build rather
// than quietly showing up as a p99.9 outlier.
//
// Skipped under ASan, which installs its own operator new and so displaces the
// counting replacements in alloc_counter.cpp.
#if !defined(LAZERBOOK_NO_ALLOC_COUNTER)

using namespace lazerbook;
using lazerbook::test::ScopedAllocCount;

namespace {

struct NullSink final : EventSink {
    void on_fill(Fill const&) override {}
    void on_accepted(OrderAccepted const&) override {}
    void on_cancelled(OrderCancelled const&) override {}
    void on_rejected(OrderRejected const&) override {}
};

}  // namespace

TEST_CASE("no-alloc: the counter itself observes an allocation") {
    // Guards against the replacements silently not being installed, which
    // would make every other case in this file vacuously pass.
    //
    // Uses ::operator new directly: a new-expression is elidable since C++14
    // and clang does elide this pair at -O2, so `new int(7)` would report zero
    // allocations and defeat the check.
    ScopedAllocCount c;
    std::size_t const n = 64 + static_cast<std::size_t>(c.allocations());
    void* p = ::operator new(n);
    CHECK(p != nullptr);
    CHECK(c.allocations() >= 1);
    ::operator delete(p);
}

TEST_CASE("no-alloc: limit insert does not allocate") {
    Book book(Price4{100000}, 4000);
    OrderPool pool(1 << 16);
    NullSink sink;
    Matcher m(book, pool, sink);

    // Warm up outside the counted region: construction allocates, by design.
    for (std::uint64_t i = 1; i <= 100; ++i) {
        m.on_new(
            OrderId{i}, Side::Buy, OrderType::Limit,
            Price4{100000U + static_cast<std::uint32_t>(i % 2000)}, 100
        );
    }

    ScopedAllocCount c;
    for (std::uint64_t i = 1000; i < 21000; ++i) {
        m.on_new(
            OrderId{i}, Side::Buy, OrderType::Limit,
            Price4{100000U + static_cast<std::uint32_t>(i % 2000)}, 100
        );
    }
    CHECK(m.resting_count() > 19000);
    CHECK(c.allocations() == 0);
}

TEST_CASE("no-alloc: cancel does not allocate") {
    Book book(Price4{100000}, 4000);
    OrderPool pool(1 << 16);
    NullSink sink;
    Matcher m(book, pool, sink);
    for (std::uint64_t i = 1; i <= 20000; ++i) {
        m.on_new(
            OrderId{i}, Side::Buy, OrderType::Limit,
            Price4{100000U + static_cast<std::uint32_t>(i % 2000)}, 100
        );
    }

    ScopedAllocCount c;
    for (std::uint64_t i = 1; i <= 20000; ++i) {
        m.on_cancel(OrderId{i});
    }
    CHECK(m.resting_count() == 0);
    CHECK(c.allocations() == 0);
}

TEST_CASE("no-alloc: matching and partial fills do not allocate") {
    Book book(Price4{100000}, 4000);
    OrderPool pool(1 << 16);
    NullSink sink;
    Matcher m(book, pool, sink);
    // Deep resting ask for the aggressors to eat into.
    m.on_new(OrderId{1}, Side::Sell, OrderType::Limit, Price4{101000}, 4000000);
    for (int k = 1; k <= 8; ++k) {
        m.on_new(
            OrderId{static_cast<std::uint64_t>(k) + 1}, Side::Sell, OrderType::Limit,
            Price4{101000U + static_cast<std::uint32_t>(k)}, 1000
        );
    }

    ScopedAllocCount c;
    for (std::uint64_t i = 100; i < 20100; ++i) {
        m.on_new(OrderId{i}, Side::Buy, OrderType::Ioc, Price4{101000}, 100);
    }
    CHECK(c.allocations() == 0);
}

TEST_CASE("no-alloc: cancel-replace does not allocate") {
    Book book(Price4{100000}, 4000);
    OrderPool pool(1 << 16);
    NullSink sink;
    Matcher m(book, pool, sink);
    for (std::uint64_t i = 1; i <= 5000; ++i) {
        m.on_new(
            OrderId{i}, Side::Buy, OrderType::Limit,
            Price4{100000U + static_cast<std::uint32_t>(i % 2000)}, 100
        );
    }

    ScopedAllocCount c;
    std::uint64_t next = 1'000'000;
    for (std::uint64_t i = 1; i <= 5000; ++i) {
        m.on_modify(
            OrderId{i}, OrderId{next}, Price4{100000U + static_cast<std::uint32_t>(next % 2000)}, 50
        );
        ++next;
    }
    CHECK(c.allocations() == 0);
}

TEST_CASE("no-alloc: a mixed add/cancel/execute stream does not allocate") {
    // The workload the throughput claim is measured on.
    Book book(Price4{100000}, 4000);
    OrderPool pool(1 << 17);
    NullSink sink;
    Matcher m(book, pool, sink);

    std::uint64_t state = 0x5EED5EED;
    auto rng = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    // Pre-seed so cancels have something to hit.
    for (std::uint64_t i = 1; i <= 30000; ++i) {
        m.on_new(
            OrderId{i}, (i & 1U) ? Side::Buy : Side::Sell, OrderType::Limit,
            Price4{100000U + static_cast<std::uint32_t>(i % 2000)}, 100
        );
    }

    ScopedAllocCount c;
    std::uint64_t next_id = 100000;
    std::uint64_t cancel_id = 1;
    for (int i = 0; i < 60000; ++i) {
        std::uint64_t const r = rng();
        std::uint64_t const roll = r % 100;
        if (roll < 45) {
            m.on_new(
                OrderId{next_id++}, (r & 8U) ? Side::Buy : Side::Sell, OrderType::Limit,
                Price4{100000U + static_cast<std::uint32_t>(r % 2000)}, 100
            );
        } else if (roll < 95) {
            m.on_cancel(OrderId{cancel_id++});
        } else {
            m.on_new(
                OrderId{next_id++}, (r & 8U) ? Side::Buy : Side::Sell, OrderType::Ioc,
                Price4{100000U + static_cast<std::uint32_t>(r % 2000)}, 50
            );
        }
    }
    CHECK(c.allocations() == 0);
}

TEST_CASE("no-alloc: reconstructor add/reduce/delete does not allocate") {
    Book book(Price4{50000}, 1024);
    OrderPool pool(1 << 16);
    Reconstructor recon(book, pool);

    itch::AddOrder add{};
    add.stock = Symbol{};
    for (std::uint64_t i = 1; i <= 100; ++i) {
        add.order_reference_number = OrderId{i};
        add.buy_sell_indicator = Side::Buy;
        add.shares = 100;
        add.price = Price4{50000U + static_cast<std::uint32_t>(i % 500)};
        recon.apply(itch::Message{add});
    }

    ScopedAllocCount c;
    for (std::uint64_t i = 1000; i < 21000; ++i) {
        add.order_reference_number = OrderId{i};
        add.buy_sell_indicator = (i & 1U) ? Side::Buy : Side::Sell;
        add.shares = 100;
        add.price = Price4{50000U + static_cast<std::uint32_t>(i % 500)};
        recon.apply(itch::Message{add});
    }
    itch::OrderDelete del{};
    for (std::uint64_t i = 1000; i < 21000; ++i) {
        del.order_reference_number = OrderId{i};
        recon.apply(itch::Message{del});
    }
    CHECK(recon.live_order_count() == 100);
    CHECK(c.allocations() == 0);
}

TEST_CASE("no-alloc: OrderIndex insert and erase do not allocate") {
    OrderIndex idx(1 << 16);
    for (std::uint64_t i = 1; i <= 100; ++i) {
        idx.insert(OrderId{i}, nullptr);
    }
    ScopedAllocCount c;
    for (std::uint64_t i = 1000; i < 21000; ++i) {
        CHECK(idx.insert(OrderId{i}, nullptr));
    }
    for (std::uint64_t i = 1000; i < 21000; ++i) {
        CHECK(idx.erase(OrderId{i}));
    }
    CHECK(c.allocations() == 0);
}

#endif  // LAZERBOOK_NO_ALLOC_COUNTER
