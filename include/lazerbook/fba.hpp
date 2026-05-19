#ifndef LAZERBOOK_FBA_HPP
#define LAZERBOOK_FBA_HPP

#include <lazerbook/book.hpp>
#include <lazerbook/events.hpp>
#include <lazerbook/order_pool.hpp>
#include <lazerbook/types.hpp>

#include <cstdint>
#include <unordered_map>

namespace lazerbook {

// Frequent Batch Auction matcher (Budish/Cramton/Shim 2015). Orders accumulate
// in the book during a batch window with no matching; clear() runs a discrete
// uniform-price auction over everything resting and emits the fills.
class FbaMatcher {
   public:
    FbaMatcher(Book& book, OrderPool& pool, EventSink& sink, std::uint64_t seed = 0xBEEFCAFEULL);

    // Limit orders only: rest immediately, never match intra-window.
    void on_new(OrderId id, Side side, Price4 price, std::uint32_t shares);
    void on_cancel(OrderId id);

    // Run the uniform-price auction. Returns the cleared volume (shares).
    std::uint64_t clear();

    [[nodiscard]] std::uint64_t batch_number() const noexcept { return batch_number_; }
    [[nodiscard]] std::size_t resting_count() const noexcept { return resting_.size(); }

   private:
    Book& book_;
    OrderPool& pool_;
    EventSink& sink_;
    std::uint64_t seed_;
    std::uint64_t batch_number_ = 0;
    std::unordered_map<std::uint64_t, Order*> resting_;
};

}  // namespace lazerbook

#endif  // LAZERBOOK_FBA_HPP
