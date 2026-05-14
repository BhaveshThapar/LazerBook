#ifndef LAZERBOOK_SPSC_RING_HPP
#define LAZERBOOK_SPSC_RING_HPP

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

namespace lazerbook {

// Single-producer / single-consumer lock-free ring (Vyukov bounded queue). The
// producer owns tail_, the consumer owns head_; each is on its own cacheline,
// and each side caches the other's cursor so the common path avoids an atomic
// acquire-load. Capacity must be a power of two; T must be trivially copyable.
template <typename T, std::size_t Capacity>
class SpscRing {
    static_assert(std::has_single_bit(Capacity), "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

   public:
    SpscRing() = default;
    SpscRing(SpscRing const&) = delete;
    SpscRing& operator=(SpscRing const&) = delete;
    SpscRing(SpscRing&&) = delete;
    SpscRing& operator=(SpscRing&&) = delete;
    ~SpscRing() = default;

    [[nodiscard]] bool try_push(T const& item) noexcept {
        std::uint64_t const tail = tail_.load(std::memory_order_relaxed);
        std::uint64_t const next = tail + 1;
        if (next - head_cached_ > Capacity) {
            head_cached_ = head_.load(std::memory_order_acquire);
            if (next - head_cached_ > Capacity) {
                return false;  // full
            }
        }
        buffer_[tail & kMask] = item;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_pop(T& out) noexcept {
        std::uint64_t const head = head_.load(std::memory_order_relaxed);
        if (head == tail_cached_) {
            tail_cached_ = tail_.load(std::memory_order_acquire);
            if (head == tail_cached_) {
                return false;  // empty
            }
        }
        out = buffer_[head & kMask];
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t size_approx() const noexcept {
        std::uint64_t const tail = tail_.load(std::memory_order_acquire);
        std::uint64_t const head = head_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(tail - head);
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

   private:
    static constexpr std::uint64_t kMask = Capacity - 1;
    static constexpr std::size_t kLine = 64;

    alignas(kLine) std::atomic<std::uint64_t> head_{0};
    std::uint64_t tail_cached_{0};  // consumer-side cache of tail_
    alignas(kLine) std::atomic<std::uint64_t> tail_{0};
    std::uint64_t head_cached_{0};  // producer-side cache of head_
    alignas(kLine) std::array<T, Capacity> buffer_{};
};

}  // namespace lazerbook

#endif  // LAZERBOOK_SPSC_RING_HPP
