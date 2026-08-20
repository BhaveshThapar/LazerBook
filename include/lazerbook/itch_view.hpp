#ifndef LAZERBOOK_ITCH_VIEW_HPP
#define LAZERBOOK_ITCH_VIEW_HPP

#include <lazerbook/itch.hpp>
#include <lazerbook/types.hpp>

#include <cstdint>
#include <span>

// Zero-copy view over an ITCH 5.0 message that is already in memory.
//
// itch::parse() materialises an owning std::variant whose largest alternative
// carries a 64-byte body array, so every message costs ~100 bytes of copying
// whether or not the caller reads those fields. At 420M messages that is the
// single biggest avoidable cost in a full-day replay.
//
// A view holds only a pointer. Field accessors decode on demand, so a handler
// that reads three fields of an AddOrder pays for three byteswaps and nothing
// else. Nothing here reinterpret_casts the buffer to a packed struct: ITCH
// messages are not aligned in the stream, and doing so would be undefined --
// the accessors go through the same unaligned-safe read_be helpers as parse().
namespace lazerbook::itch {

// Body always begins at offset 11 (1 type + 2 locate + 2 tracking + 6 ts).
inline constexpr std::size_t kHeaderLen = 11;

// Common header fields, available on every message type.
class MessageView {
   public:
    explicit constexpr MessageView(std::uint8_t const* p) noexcept : p_(p) {}

    [[nodiscard]] MessageType type() const noexcept {
        return static_cast<MessageType>(static_cast<char>(p_[0]));
    }
    [[nodiscard]] std::uint16_t stock_locate() const noexcept {
        return read_be<std::uint16_t>(p_ + 1);
    }
    [[nodiscard]] std::uint16_t tracking_number() const noexcept {
        return read_be<std::uint16_t>(p_ + 3);
    }
    [[nodiscard]] Timestamp timestamp() const noexcept { return Timestamp{read_be48(p_ + 5)}; }

    [[nodiscard]] Header header() const noexcept {
        return Header{type(), stock_locate(), tracking_number(), timestamp()};
    }
    [[nodiscard]] std::uint8_t const* data() const noexcept { return p_; }

   protected:
    std::uint8_t const* p_;
};

// --- Order-book messages (the hot path) -------------------------------------

class AddOrderView : public MessageView {
   public:
    explicit constexpr AddOrderView(std::uint8_t const* p) noexcept : MessageView(p) {}

    [[nodiscard]] OrderId order_reference_number() const noexcept {
        return OrderId{read_be<std::uint64_t>(p_ + 11)};
    }
    [[nodiscard]] Side buy_sell_indicator() const noexcept {
        return static_cast<Side>(static_cast<char>(p_[19]));
    }
    [[nodiscard]] std::uint32_t shares() const noexcept { return read_be<std::uint32_t>(p_ + 20); }
    [[nodiscard]] Symbol stock() const noexcept { return read_symbol(p_ + 24); }
    [[nodiscard]] Price4 price() const noexcept { return read_be<Price4>(p_ + 32); }
};

// 'F' shares AddOrder's layout and appends a 4-char attribution.
class AddOrderMpidView : public AddOrderView {
   public:
    explicit constexpr AddOrderMpidView(std::uint8_t const* p) noexcept : AddOrderView(p) {}

    [[nodiscard]] std::array<char, 4> attribution() const noexcept {
        return {
            static_cast<char>(p_[36]), static_cast<char>(p_[37]), static_cast<char>(p_[38]),
            static_cast<char>(p_[39])
        };
    }
};

class OrderExecutedView : public MessageView {
   public:
    explicit constexpr OrderExecutedView(std::uint8_t const* p) noexcept : MessageView(p) {}

    [[nodiscard]] OrderId order_reference_number() const noexcept {
        return OrderId{read_be<std::uint64_t>(p_ + 11)};
    }
    [[nodiscard]] std::uint32_t executed_shares() const noexcept {
        return read_be<std::uint32_t>(p_ + 19);
    }
    [[nodiscard]] std::uint64_t match_number() const noexcept {
        return read_be<std::uint64_t>(p_ + 23);
    }
};

class OrderExecutedWithPriceView : public OrderExecutedView {
   public:
    explicit constexpr OrderExecutedWithPriceView(std::uint8_t const* p) noexcept
        : OrderExecutedView(p) {}

    [[nodiscard]] char printable() const noexcept { return static_cast<char>(p_[31]); }
    [[nodiscard]] Price4 execution_price() const noexcept { return read_be<Price4>(p_ + 32); }
};

class OrderCancelView : public MessageView {
   public:
    explicit constexpr OrderCancelView(std::uint8_t const* p) noexcept : MessageView(p) {}

    [[nodiscard]] OrderId order_reference_number() const noexcept {
        return OrderId{read_be<std::uint64_t>(p_ + 11)};
    }
    [[nodiscard]] std::uint32_t cancelled_shares() const noexcept {
        return read_be<std::uint32_t>(p_ + 19);
    }
};

class OrderDeleteView : public MessageView {
   public:
    explicit constexpr OrderDeleteView(std::uint8_t const* p) noexcept : MessageView(p) {}

    [[nodiscard]] OrderId order_reference_number() const noexcept {
        return OrderId{read_be<std::uint64_t>(p_ + 11)};
    }
};

class OrderReplaceView : public MessageView {
   public:
    explicit constexpr OrderReplaceView(std::uint8_t const* p) noexcept : MessageView(p) {}

    [[nodiscard]] OrderId original_order_reference_number() const noexcept {
        return OrderId{read_be<std::uint64_t>(p_ + 11)};
    }
    [[nodiscard]] OrderId new_order_reference_number() const noexcept {
        return OrderId{read_be<std::uint64_t>(p_ + 19)};
    }
    [[nodiscard]] std::uint32_t shares() const noexcept { return read_be<std::uint32_t>(p_ + 27); }
    [[nodiscard]] Price4 price() const noexcept { return read_be<Price4>(p_ + 31); }
};

// --- Trade messages ---------------------------------------------------------

class TradeView : public MessageView {
   public:
    explicit constexpr TradeView(std::uint8_t const* p) noexcept : MessageView(p) {}

    [[nodiscard]] OrderId order_reference_number() const noexcept {
        return OrderId{read_be<std::uint64_t>(p_ + 11)};
    }
    [[nodiscard]] Side buy_sell_indicator() const noexcept {
        return static_cast<Side>(static_cast<char>(p_[19]));
    }
    [[nodiscard]] std::uint32_t shares() const noexcept { return read_be<std::uint32_t>(p_ + 20); }
    [[nodiscard]] Symbol stock() const noexcept { return read_symbol(p_ + 24); }
    [[nodiscard]] Price4 price() const noexcept { return read_be<Price4>(p_ + 32); }
    [[nodiscard]] std::uint64_t match_number() const noexcept {
        return read_be<std::uint64_t>(p_ + 36);
    }
};

class CrossTradeView : public MessageView {
   public:
    explicit constexpr CrossTradeView(std::uint8_t const* p) noexcept : MessageView(p) {}

    [[nodiscard]] std::uint64_t shares() const noexcept { return read_be<std::uint64_t>(p_ + 11); }
    [[nodiscard]] Symbol stock() const noexcept { return read_symbol(p_ + 19); }
    [[nodiscard]] Price4 cross_price() const noexcept { return read_be<Price4>(p_ + 27); }
    [[nodiscard]] std::uint64_t match_number() const noexcept {
        return read_be<std::uint64_t>(p_ + 31);
    }
    [[nodiscard]] char cross_type() const noexcept { return static_cast<char>(p_[39]); }
};

class BrokenTradeView : public MessageView {
   public:
    explicit constexpr BrokenTradeView(std::uint8_t const* p) noexcept : MessageView(p) {}

    [[nodiscard]] std::uint64_t match_number() const noexcept {
        return read_be<std::uint64_t>(p_ + 11);
    }
};

// --- Administrative messages ------------------------------------------------

class SystemEventView : public MessageView {
   public:
    explicit constexpr SystemEventView(std::uint8_t const* p) noexcept : MessageView(p) {}
    [[nodiscard]] char event_code() const noexcept { return static_cast<char>(p_[11]); }
};

class StockDirectoryView : public MessageView {
   public:
    explicit constexpr StockDirectoryView(std::uint8_t const* p) noexcept : MessageView(p) {}

    [[nodiscard]] Symbol stock() const noexcept { return read_symbol(p_ + 11); }
    [[nodiscard]] char market_category() const noexcept { return static_cast<char>(p_[19]); }
    [[nodiscard]] char financial_status_indicator() const noexcept {
        return static_cast<char>(p_[20]);
    }
    [[nodiscard]] std::uint32_t round_lot_size() const noexcept {
        return read_be<std::uint32_t>(p_ + 21);
    }
};

class StockTradingActionView : public MessageView {
   public:
    explicit constexpr StockTradingActionView(std::uint8_t const* p) noexcept : MessageView(p) {}

    [[nodiscard]] Symbol stock() const noexcept { return read_symbol(p_ + 11); }
    [[nodiscard]] char trading_state() const noexcept { return static_cast<char>(p_[19]); }
};

class RegShoView : public MessageView {
   public:
    explicit constexpr RegShoView(std::uint8_t const* p) noexcept : MessageView(p) {}

    [[nodiscard]] Symbol stock() const noexcept { return read_symbol(p_ + 11); }
    [[nodiscard]] char reg_sho_action() const noexcept { return static_cast<char>(p_[19]); }
};

class NoiiView : public MessageView {
   public:
    explicit constexpr NoiiView(std::uint8_t const* p) noexcept : MessageView(p) {}

    [[nodiscard]] std::uint64_t paired_shares() const noexcept {
        return read_be<std::uint64_t>(p_ + 11);
    }
    [[nodiscard]] std::uint64_t imbalance_shares() const noexcept {
        return read_be<std::uint64_t>(p_ + 19);
    }
    [[nodiscard]] char imbalance_direction() const noexcept { return static_cast<char>(p_[27]); }
    [[nodiscard]] Symbol stock() const noexcept { return read_symbol(p_ + 28); }
    [[nodiscard]] Price4 far_price() const noexcept { return read_be<Price4>(p_ + 36); }
    [[nodiscard]] Price4 near_price() const noexcept { return read_be<Price4>(p_ + 40); }
    [[nodiscard]] Price4 current_reference_price() const noexcept {
        return read_be<Price4>(p_ + 44);
    }
};

// --- Dispatch ---------------------------------------------------------------

// Calls handler(SomeView const&) for the message at the start of `buf`, or
// handler.on_unhandled(MessageView) for types with no dedicated view. Returns
// the message length consumed, or 0 if the buffer is short or the type byte is
// not recognised.
//
// The handler supplies only the overloads it cares about plus a catch-all;
// everything else compiles to a jump-table entry that does nothing.
template <typename Handler>
std::size_t visit(std::span<std::uint8_t const> buf, Handler& handler) noexcept {
    if (buf.size() < kHeaderLen) {
        return 0;
    }
    std::uint8_t const* p = buf.data();
    auto const type = static_cast<MessageType>(static_cast<char>(p[0]));
    std::size_t const need = expected_length(type);
    if (need == 0 || buf.size() < need) {
        return 0;
    }

    switch (type) {
        case MessageType::AddOrder:
            handler.on_message(AddOrderView{p});
            break;
        case MessageType::AddOrderMpid:
            handler.on_message(AddOrderMpidView{p});
            break;
        case MessageType::OrderExecuted:
            handler.on_message(OrderExecutedView{p});
            break;
        case MessageType::OrderExecutedWithPrice:
            handler.on_message(OrderExecutedWithPriceView{p});
            break;
        case MessageType::OrderCancel:
            handler.on_message(OrderCancelView{p});
            break;
        case MessageType::OrderDelete:
            handler.on_message(OrderDeleteView{p});
            break;
        case MessageType::OrderReplace:
            handler.on_message(OrderReplaceView{p});
            break;
        case MessageType::Trade:
            handler.on_message(TradeView{p});
            break;
        case MessageType::CrossTrade:
            handler.on_message(CrossTradeView{p});
            break;
        case MessageType::BrokenTrade:
            handler.on_message(BrokenTradeView{p});
            break;
        case MessageType::SystemEvent:
            handler.on_message(SystemEventView{p});
            break;
        case MessageType::StockDirectory:
            handler.on_message(StockDirectoryView{p});
            break;
        case MessageType::StockTradingAction:
            handler.on_message(StockTradingActionView{p});
            break;
        case MessageType::RegSho:
            handler.on_message(RegShoView{p});
            break;
        case MessageType::Noii:
            handler.on_message(NoiiView{p});
            break;
        default:
            // Recognised length, no dedicated view (L, V, W, K, J, h, N).
            handler.on_unhandled(MessageView{p});
            break;
    }
    return need;
}

}  // namespace lazerbook::itch

#endif  // LAZERBOOK_ITCH_VIEW_HPP
