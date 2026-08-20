#include "alloc_counter.hpp"

#include <cstdlib>
#include <new>

// Program-wide replacement of the global allocation functions. Must be defined
// exactly once, hence its own translation unit.
//
// Under AddressSanitizer these replacements are not installed -- ASan provides
// its own operator new/delete to track allocations, and defining ours too is
// undefined. The tests that depend on counting are compiled out to match.
namespace lazerbook::test {

std::atomic<std::uint64_t> g_new_calls{0};
std::atomic<std::uint64_t> g_delete_calls{0};

}  // namespace lazerbook::test

#if !defined(LAZERBOOK_NO_ALLOC_COUNTER)

namespace {

void* counted_alloc(std::size_t n) {
    lazerbook::test::g_new_calls.fetch_add(1, std::memory_order_relaxed);
    // operator new must never return nullptr; zero-size must still be unique.
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}

void counted_free(void* p) noexcept {
    if (p != nullptr) {
        lazerbook::test::g_delete_calls.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(p);
}

}  // namespace

void* operator new(std::size_t n) { return counted_alloc(n); }
void* operator new[](std::size_t n) { return counted_alloc(n); }

void* operator new(std::size_t n, std::nothrow_t const&) noexcept {
    lazerbook::test::g_new_calls.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(n == 0 ? 1 : n);
}
void* operator new[](std::size_t n, std::nothrow_t const&) noexcept {
    lazerbook::test::g_new_calls.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(n == 0 ? 1 : n);
}

void operator delete(void* p) noexcept { counted_free(p); }
void operator delete[](void* p) noexcept { counted_free(p); }
void operator delete(void* p, std::size_t) noexcept { counted_free(p); }
void operator delete[](void* p, std::size_t) noexcept { counted_free(p); }
void operator delete(void* p, std::nothrow_t const&) noexcept { counted_free(p); }
void operator delete[](void* p, std::nothrow_t const&) noexcept { counted_free(p); }

#endif  // LAZERBOOK_NO_ALLOC_COUNTER
