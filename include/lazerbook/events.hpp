#ifndef LAZERBOOK_EVENTS_HPP
#define LAZERBOOK_EVENTS_HPP

#include <lazerbook/types.hpp>

#include <cstdint>

namespace lazerbook {

enum class RejectReason : std::uint8_t {
    None,
    PoolExhausted,
    DuplicateOrderId,
    OrderNotFound,
    PriceOutOfRange,
    FokWouldNotFullyFill,
    InvalidShares,
};

struct Fill {
    OrderId aggressor_id;
    OrderId passive_id;
    Price4 price;
    std::uint32_t shares;
    Side aggressor_side;
};

struct OrderAccepted {
    OrderId id;
    Side side;
    Price4 price;
    std::uint32_t shares;
};

struct OrderCancelled {
    OrderId id;
    std::uint32_t remaining;
};

struct OrderRejected {
    OrderId id;
    RejectReason reason;
};

// Sink for matcher output. Implementations must not throw.
class EventSink {
   public:
    EventSink() = default;
    EventSink(EventSink const&) = default;
    EventSink(EventSink&&) = default;
    EventSink& operator=(EventSink const&) = default;
    EventSink& operator=(EventSink&&) = default;
    virtual ~EventSink() = default;

    virtual void on_fill(Fill const&) = 0;
    virtual void on_accepted(OrderAccepted const&) = 0;
    virtual void on_cancelled(OrderCancelled const&) = 0;
    virtual void on_rejected(OrderRejected const&) = 0;
};

}  // namespace lazerbook

#endif  // LAZERBOOK_EVENTS_HPP
