#include <lazerbook/spsc_ring.hpp>

#include <atomic>
#include <cstdint>
#include <doctest/doctest.h>
#include <thread>

using namespace lazerbook;

TEST_CASE("single-threaded push/pop preserves FIFO order") {
    SpscRing<std::uint32_t, 8> ring;
    CHECK(ring.capacity() == 8);
    for (std::uint32_t i = 0; i < 7; ++i) {
        CHECK(ring.try_push(i));
    }
    std::uint32_t v = 0;
    CHECK(ring.try_pop(v));
    CHECK(v == 0);
    CHECK(ring.try_pop(v));
    CHECK(v == 1);
}

TEST_CASE("push fails when the ring is full") {
    SpscRing<std::uint32_t, 4> ring;
    CHECK(ring.try_push(1));
    CHECK(ring.try_push(2));
    CHECK(ring.try_push(3));
    CHECK(ring.try_push(4));
    CHECK_FALSE(ring.try_push(5));  // full at capacity
}

TEST_CASE("pop fails when the ring is empty") {
    SpscRing<std::uint32_t, 4> ring;
    std::uint32_t v = 0;
    CHECK_FALSE(ring.try_pop(v));
}

TEST_CASE("100k-element threaded round trip is lossless and ordered") {
    constexpr std::uint64_t kCount = 100000;
    SpscRing<std::uint64_t, 1024> ring;

    std::thread producer([&ring] {
        for (std::uint64_t i = 0; i < kCount; ++i) {
            while (!ring.try_push(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::uint64_t received = 0;
    std::uint64_t expected = 0;
    bool ordered = true;
    std::thread consumer([&] {
        std::uint64_t v = 0;
        while (received < kCount) {
            if (ring.try_pop(v)) {
                if (v != expected) {
                    ordered = false;
                }
                ++expected;
                ++received;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();
    CHECK(received == kCount);
    CHECK(ordered);
}
