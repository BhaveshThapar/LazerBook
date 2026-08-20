#ifndef LAZERBOOK_ORDER_POOL_HPP
#define LAZERBOOK_ORDER_POOL_HPP

#include <lazerbook/order.hpp>

#include <cstddef>
#include <vector>

namespace lazerbook {

// Fixed-capacity slab allocator for Orders. A single contiguous vector backs
// every order; free slots are threaded into a singly-linked freelist through
// Order::next. acquire()/release() are O(1), noexcept, and allocation-free in
// steady state. Not thread-safe: the matcher thread is the sole owner.
class OrderPool {
   public:
    explicit OrderPool(std::size_t capacity) : storage_(capacity), capacity_(capacity) {
        // Thread the freelist newest-first so acquire() returns slot 0 first.
        free_head_ = nullptr;
        for (std::size_t i = capacity_; i-- > 0;) {
            storage_[i].next = free_head_;
            free_head_ = &storage_[i];
        }
    }

    OrderPool(OrderPool const&) = delete;
    OrderPool& operator=(OrderPool const&) = delete;
    OrderPool(OrderPool&&) = delete;
    OrderPool& operator=(OrderPool&&) = delete;
    ~OrderPool() = default;

    // Returns nullptr when exhausted.
    [[nodiscard]] Order* acquire() noexcept {
        if (free_head_ == nullptr) {
            return nullptr;
        }
        Order* o = free_head_;
        free_head_ = o->next;
        o->prev = nullptr;
        o->next = nullptr;
        ++in_use_;
        return o;
    }

    void release(Order* o) noexcept {
        o->next = free_head_;
        o->prev = nullptr;
        free_head_ = o;
        --in_use_;
    }

    // Slot index of an order in this pool. Lets a caller key side tables by
    // slot instead of adding fields to Order, which is held at 40 bytes so it
    // shares a cache line with its neighbour.
    // Precondition: o was acquired from this pool.
    [[nodiscard]] std::size_t index_of(Order const* o) const noexcept {
        return static_cast<std::size_t>(o - storage_.data());
    }

    [[nodiscard]] std::size_t in_use() const noexcept { return in_use_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t available() const noexcept { return capacity_ - in_use_; }

   private:
    std::vector<Order> storage_;
    Order* free_head_ = nullptr;
    std::size_t in_use_ = 0;
    std::size_t capacity_ = 0;
};

}  // namespace lazerbook

#endif  // LAZERBOOK_ORDER_POOL_HPP
