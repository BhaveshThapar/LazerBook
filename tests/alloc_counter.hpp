#ifndef LAZERBOOK_TESTS_ALLOC_COUNTER_HPP
#define LAZERBOOK_TESTS_ALLOC_COUNTER_HPP

#include <atomic>
#include <cstdint>

// Global operator new/delete are replaced in alloc_counter.cpp so a test can
// assert that a region of code allocates nothing at all. This is what turns
// "no allocation on the hot path" from a comment into a check that fails.
namespace lazerbook::test {

extern std::atomic<std::uint64_t> g_new_calls;
extern std::atomic<std::uint64_t> g_delete_calls;

// Records the allocation count at construction; allocations() reports how many
// have happened since. Deliberately not asserting in the destructor -- doctest
// macros read better at the call site than a hidden failure at scope exit.
class ScopedAllocCount {
   public:
    ScopedAllocCount() noexcept : new_start_(g_new_calls.load(std::memory_order_relaxed)) {}

    [[nodiscard]] std::uint64_t allocations() const noexcept {
        return g_new_calls.load(std::memory_order_relaxed) - new_start_;
    }

   private:
    std::uint64_t new_start_;
};

}  // namespace lazerbook::test

#endif  // LAZERBOOK_TESTS_ALLOC_COUNTER_HPP
