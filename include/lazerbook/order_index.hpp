#ifndef LAZERBOOK_ORDER_INDEX_HPP
#define LAZERBOOK_ORDER_INDEX_HPP

#include <lazerbook/order.hpp>
#include <lazerbook/types.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lazerbook {

// Open-addressed order-id -> Order* index, allocated once and never resized.
//
// Replaces std::unordered_map, which allocates a node per resting order and
// periodically rehashes -- both on the insert path, and both inside the region
// the latency benchmark measures.
//
// Linear probing with backward-shift deletion, so there are no tombstones and
// probe lengths stay bounded by the live load factor rather than by the
// historical one. Capacity is a power of two and fixed at construction:
// growing would allocate, so a full table is a counted rejection instead,
// exactly like the existing OrderPool exhaustion path.
class OrderIndex {
   public:
    // capacity is rounded up to a power of two. Keep the load factor at or
    // below 0.5 for short probes; size for peak *live* orders, not total.
    explicit OrderIndex(std::size_t capacity)
        : slots_(std::bit_ceil(capacity < 8 ? std::size_t{8} : capacity)),
          mask_(slots_.size() - 1) {}

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size(); }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    // Returns false if the id is already present or the table is full.
    // Never allocates.
    bool insert(OrderId id, Order* o) noexcept {
        // Keep one slot free so a full table cannot loop forever on lookup.
        if (size_ + 1 >= slots_.size()) {
            return false;
        }
        std::uint64_t const key = value_of(id);
        std::size_t i = bucket_of(key);
        while (slots_[i].occupied) {
            if (slots_[i].key == key) {
                return false;
            }
            i = (i + 1) & mask_;
        }
        slots_[i] = Slot{key, o, true};
        ++size_;
        return true;
    }

    [[nodiscard]] Order* find(OrderId id) const noexcept {
        std::uint64_t const key = value_of(id);
        std::size_t i = bucket_of(key);
        while (slots_[i].occupied) {
            if (slots_[i].key == key) {
                return slots_[i].value;
            }
            i = (i + 1) & mask_;
        }
        return nullptr;
    }

    [[nodiscard]] bool contains(OrderId id) const noexcept { return find(id) != nullptr; }

    // Returns false if the id was not present.
    bool erase(OrderId id) noexcept {
        std::uint64_t const key = value_of(id);
        std::size_t i = bucket_of(key);
        while (slots_[i].occupied) {
            if (slots_[i].key == key) {
                remove_at(i);
                return true;
            }
            i = (i + 1) & mask_;
        }
        return false;
    }

    void clear() noexcept {
        for (Slot& s : slots_) {
            s.occupied = false;
        }
        size_ = 0;
    }

    // Longest contiguous run of occupied slots, which bounds the work a single
    // probe or backward-shift deletion can do. Diagnostic only -- O(capacity).
    [[nodiscard]] std::size_t longest_run() const noexcept {
        std::size_t const n = slots_.size();
        std::size_t best = 0;
        std::size_t run = 0;
        // Two passes so a run wrapping the end of the table is counted whole.
        for (std::size_t i = 0; i < 2 * n; ++i) {
            if (slots_[i & mask_].occupied) {
                ++run;
                best = (run > best) ? run : best;
            } else {
                run = 0;
            }
        }
        return (best > n) ? n : best;
    }

    // Prefetch the bucket an id will probe first. The framed message stream is
    // sequential, so the pipeline can issue this for message i+k while message
    // i is still being applied, hiding the DRAM latency of a table that does
    // not fit in L3.
    void prefetch(OrderId id) const noexcept {
#if defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(&slots_[bucket_of(value_of(id))], 0, 3);
#else
        (void)id;
#endif
    }

   private:
    struct Slot {
        std::uint64_t key = 0;
        Order* value = nullptr;
        bool occupied = false;
    };

    [[nodiscard]] std::size_t bucket_of(std::uint64_t key) const noexcept {
        // Full avalanche (splitmix64 finalizer), deliberately, even though ITCH
        // order references are near-sequential and a locality-preserving hash
        // measures ~3x better on insert.
        //
        // The reason is deletion. A hash that maps consecutive refs to
        // consecutive buckets packs them into one enormous contiguous run, and
        // backward-shift deletion has to scan forward to the next free slot --
        // so cancelling in insertion order degrades to O(n^2). Measured: p50
        // cancel went from 42ns to 36.9us on a 100k-order book, a ~900x
        // regression, while insert p99 improved by only ~83ns.
        //
        // Scattering keeps runs short (load factor <= 0.5), which makes both
        // insert and erase O(1) amortised. See the cancel-in-insertion-order
        // case in tests/test_order_index.cpp.
        std::uint64_t h = key;
        h ^= h >> 30;
        h *= 0xBF58476D1CE4E5B9ULL;
        h ^= h >> 27;
        h *= 0x94D049BB133111EBULL;
        h ^= h >> 31;
        return static_cast<std::size_t>(h) & mask_;
    }

    // Backward-shift deletion: pull following entries back into the hole when
    // they probed past it, which keeps every chain contiguous without needing
    // tombstones.
    //
    // Two cursors: `hole` is the slot to fill, `scan` walks forward. An entry
    // at `scan` may move back only if its ideal bucket is at or before `hole`
    // in probe order -- otherwise moving it would put it before its own bucket
    // and make it unfindable. Terminates because insert() always leaves at
    // least one unoccupied slot.
    void remove_at(std::size_t hole) noexcept {
        std::size_t scan = hole;
        while (true) {
            scan = (scan + 1) & mask_;
            if (!slots_[scan].occupied) {
                break;
            }
            std::size_t const ideal = bucket_of(slots_[scan].key);
            if (((scan - ideal) & mask_) >= ((scan - hole) & mask_)) {
                slots_[hole] = slots_[scan];
                hole = scan;
            }
        }
        slots_[hole].occupied = false;
        slots_[hole].value = nullptr;
        --size_;
    }

    std::vector<Slot> slots_;
    std::size_t mask_;
    std::size_t size_ = 0;
};

}  // namespace lazerbook

#endif  // LAZERBOOK_ORDER_INDEX_HPP
