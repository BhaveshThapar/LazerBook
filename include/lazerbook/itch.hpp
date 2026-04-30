#ifndef LAZERBOOK_ITCH_HPP
#define LAZERBOOK_ITCH_HPP

#include <lazerbook/types.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <variant>

// NASDAQ TotalView-ITCH 5.0 decoder. Operates on a span whose first byte is the
// message-type byte (no length prefix). All multi-byte integers are big-endian;
// prices are Price4 (1/10000 dollar); timestamps are 6-byte BE nanoseconds.
namespace lazerbook::itch {

enum class MessageType : char {
    None = 0,
    SystemEvent = 'S',
    StockDirectory = 'R',
    StockTradingAction = 'H',
    RegSho = 'Y',
    MarketParticipantPosition = 'L',
    MwcbDeclineLevel = 'V',
    MwcbStatus = 'W',
    IpoQuotingPeriodUpdate = 'K',
    LuldAuctionCollar = 'J',
    OperationalHalt = 'h',
    AddOrder = 'A',
    AddOrderMpid = 'F',
    OrderExecuted = 'E',
    OrderExecutedWithPrice = 'C',
    OrderCancel = 'X',
    OrderDelete = 'D',
    OrderReplace = 'U',
    Trade = 'P',
    CrossTrade = 'Q',
    BrokenTrade = 'B',
    Noii = 'I',
    Rpii = 'N',
};

struct Header {
    MessageType type{MessageType::None};
    std::uint16_t stock_locate{};
    std::uint16_t tracking_number{};
    Timestamp timestamp{};
};

// --- Fully decoded message bodies ------------------------------------------

struct SystemEvent {
    Header hdr;
    char event_code{};
};

struct StockDirectory {
    Header hdr;
    Symbol stock;
    char market_category{};
    char financial_status_indicator{};
    std::uint32_t round_lot_size{};
    char round_lots_only{};
    char issue_classification{};
    std::array<char, 2> issue_subtype{};
    char authenticity{};
    char short_sale_threshold_indicator{};
    char ipo_flag{};
    char luld_reference_price_tier{};
    char etp_flag{};
    std::uint32_t etp_leverage_factor{};
    char inverse_indicator{};
};

struct StockTradingAction {
    Header hdr;
    Symbol stock;
    char trading_state{};
    char reserved{};
    std::array<char, 4> reason{};
};

struct RegSho {
    Header hdr;
    Symbol stock;
    char reg_sho_action{};
};

struct AddOrder {
    Header hdr;
    OrderId order_reference_number{};
    Side buy_sell_indicator{};
    std::uint32_t shares{};
    Symbol stock;
    Price4 price{};
};

struct AddOrderMpid {
    Header hdr;
    OrderId order_reference_number{};
    Side buy_sell_indicator{};
    std::uint32_t shares{};
    Symbol stock;
    Price4 price{};
    std::array<char, 4> attribution{};
};

struct OrderExecuted {
    Header hdr;
    OrderId order_reference_number{};
    std::uint32_t executed_shares{};
    std::uint64_t match_number{};
};

struct OrderExecutedWithPrice {
    Header hdr;
    OrderId order_reference_number{};
    std::uint32_t executed_shares{};
    std::uint64_t match_number{};
    char printable{};
    Price4 execution_price{};
};

struct OrderCancel {
    Header hdr;
    OrderId order_reference_number{};
    std::uint32_t cancelled_shares{};
};

struct OrderDelete {
    Header hdr;
    OrderId order_reference_number{};
};

struct OrderReplace {
    Header hdr;
    OrderId original_order_reference_number{};
    OrderId new_order_reference_number{};
    std::uint32_t shares{};
    Price4 price{};
};

struct Trade {
    Header hdr;
    OrderId order_reference_number{};
    Side buy_sell_indicator{};
    std::uint32_t shares{};
    Symbol stock;
    Price4 price{};
    std::uint64_t match_number{};
};

struct CrossTrade {
    Header hdr;
    std::uint64_t shares{};
    Symbol stock;
    Price4 cross_price{};
    std::uint64_t match_number{};
    char cross_type{};
};

struct BrokenTrade {
    Header hdr;
    std::uint64_t match_number{};
};

struct Noii {
    Header hdr;
    std::uint64_t paired_shares{};
    std::uint64_t imbalance_shares{};
    char imbalance_direction{};
    Symbol stock;
    Price4 far_price{};
    Price4 near_price{};
    Price4 current_reference_price{};
    char cross_type{};
    char price_variation_indicator{};
};

// Recognised but not deeply decoded in v1 (L, V, W, K, J, h, N): header parsed,
// body bytes stashed verbatim.
struct Unknown {
    Header hdr;
    std::array<std::uint8_t, 64> raw{};
    std::uint8_t length{};
};

using Message = std::variant<
    SystemEvent, StockDirectory, StockTradingAction, RegSho, AddOrder, AddOrderMpid, OrderExecuted,
    OrderExecutedWithPrice, OrderCancel, OrderDelete, OrderReplace, Trade, CrossTrade, BrokenTrade,
    Noii, Unknown>;

enum class ParseError : std::uint8_t { BufferTooShort, UnknownMessageType };

[[nodiscard]] std::expected<Message, ParseError> parse(std::span<std::uint8_t const> buf) noexcept;

// Total wire length (including the type byte) for a known type; 0 if unknown.
[[nodiscard]] std::size_t expected_length(MessageType type) noexcept;

}  // namespace lazerbook::itch

#endif  // LAZERBOOK_ITCH_HPP
