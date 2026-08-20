#ifndef LAZERBOOK_BOOK_REGISTRY_HPP
#define LAZERBOOK_BOOK_REGISTRY_HPP

#include <lazerbook/book.hpp>
#include <lazerbook/types.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace lazerbook {

// One order book per instrument, indexed by ITCH stock locate.
//
// Every ITCH message carries stock_locate in its header -- including the
// order-only messages E/C/X/D/U, which name no symbol -- so routing is an array
// index and no message ever needs a cross-symbol lookup.
//
// Books are created lazily on first activity and their ladder is centred on the
// first price seen for that instrument. That matters for footprint: a Nasdaq
// day names ~9000 instruments but only a fraction ever trade, and eagerly
// allocating a ladder for all of them would cost hundreds of megabytes of
// permanently empty PriceLevel. Centring also removes the need to guess a
// global price range -- a $3 stock and a $400 stock get windows of the same
// width around their own level.
//
// A price outside its instrument's window has no slot. Book::in_range already
// reports that, and the caller counts it rather than rounding, since rounding
// would silently corrupt the reconstruction.
class BookRegistry {
   public:
    struct Config {
        // Window width as a percentage of the instrument's own price, so a $3
        // stock and a $400 stock each get a band that means something to them.
        // A single fixed tick count cannot: wide enough for a $400 stock is
        // 4096 penny slots, which at 32 bytes a level and two sides is 256KB
        // per book -- 2.4GB across a 9500-name day, nearly all of it slots that
        // can never be touched.
        std::uint32_t window_pct = 40;
        std::uint32_t min_ticks = 256;
        std::uint32_t max_ticks = 8192;
        // Price at or above which quoting is on the penny grid. Below it,
        // sub-penny increments are permitted, so the ladder uses tick_size 1.
        std::uint32_t penny_grid_floor = 10000;  // $1.0000 in Price4
        std::uint32_t penny_tick = 100;          // $0.01 in Price4
    };

    // Two constructors rather than a defaulted Config argument: a default
    // argument is parsed while the enclosing class is still incomplete, so
    // Config's own member initialisers are not yet usable there.
    explicit BookRegistry(std::uint16_t max_locate) : BookRegistry(max_locate, Config{}) {}

    BookRegistry(std::uint16_t max_locate, Config cfg)
        : books_(static_cast<std::size_t>(max_locate) + 1), cfg_(cfg) {}

    // Book for this instrument, or nullptr if nothing has been seen for it.
    [[nodiscard]] Book* find(std::uint16_t locate) noexcept {
        if (locate >= books_.size()) {
            return nullptr;
        }
        return books_[locate].get();
    }
    [[nodiscard]] Book const* find(std::uint16_t locate) const noexcept {
        if (locate >= books_.size()) {
            return nullptr;
        }
        return books_[locate].get();
    }

    // Book for this instrument, creating one centred on `price` if needed.
    // Returns nullptr only if the locate is outside the configured range.
    Book* get_or_create(std::uint16_t locate, Price4 price) {
        if (locate >= books_.size()) {
            return nullptr;
        }
        if (books_[locate] == nullptr) {
            books_[locate] = make_book(price);
            ++created_;
        }
        return books_[locate].get();
    }

    [[nodiscard]] std::size_t book_count() const noexcept { return created_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return books_.size(); }
    [[nodiscard]] Config const& config() const noexcept { return cfg_; }

    // Resident bytes of ladder storage, for reporting footprint.
    [[nodiscard]] std::size_t ladder_bytes() const noexcept {
        return total_ticks_ * sizeof(PriceLevel) * 2;
    }
    [[nodiscard]] std::size_t total_ticks() const noexcept { return total_ticks_; }

   private:
    [[nodiscard]] std::uint32_t ticks_for(std::uint32_t price, std::uint32_t tick) const noexcept {
        std::uint64_t const width = (static_cast<std::uint64_t>(price) * cfg_.window_pct) / 100ULL;
        std::uint64_t ticks = width / tick;
        ticks = (ticks < cfg_.min_ticks) ? cfg_.min_ticks : ticks;
        ticks = (ticks > cfg_.max_ticks) ? cfg_.max_ticks : ticks;
        return static_cast<std::uint32_t>(ticks);
    }

    [[nodiscard]] std::unique_ptr<Book> make_book(Price4 price) {
        std::uint32_t const p = value_of(price);
        std::uint32_t const tick = (p >= cfg_.penny_grid_floor) ? cfg_.penny_tick : 1U;
        std::uint32_t const ticks = ticks_for(p, tick);
        // Centre the window on the first price, snapped onto the grid so that
        // price itself has a slot.
        std::uint32_t const half = (ticks / 2) * tick;
        std::uint32_t base = (p > half) ? (p - half) : 0U;
        base -= base % tick;
        // Guarantee the seeding price lands inside the window even at the
        // bottom of the price range, where the centring clamps at zero.
        if (p < base || (p - base) / tick >= ticks) {
            base = p - (p % tick);
        }
        total_ticks_ += ticks;
        return std::make_unique<Book>(Price4{base}, ticks, tick);
    }

    std::vector<std::unique_ptr<Book>> books_;
    Config cfg_;
    std::size_t created_ = 0;
    std::size_t total_ticks_ = 0;
};

}  // namespace lazerbook

#endif  // LAZERBOOK_BOOK_REGISTRY_HPP
