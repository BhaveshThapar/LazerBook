#ifndef LAZERBOOK_ORDER_HPP
#define LAZERBOOK_ORDER_HPP

#include <lazerbook/types.hpp>

#include <array>
#include <cstdint>

namespace lazerbook {

// 40-byte POD order, also an intrusive doubly-linked list node. The pool
// owns the storage; the book threads orders onto price levels via prev/next.
struct Order {
    Order* prev;           // 8
    Order* next;           // 8
    OrderId id;            // 8
    Price4 price;          // 4
    std::uint32_t shares;  // 4
    Side side;             // 1

   private:
    [[maybe_unused]] std::array<std::uint8_t, 7> pad_{};  // 7 -> total 40
};

static_assert(sizeof(Order) <= 64, "Order must fit in a cache line");

}  // namespace lazerbook

#endif  // LAZERBOOK_ORDER_HPP
