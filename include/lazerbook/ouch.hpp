#ifndef LAZERBOOK_OUCH_HPP
#define LAZERBOOK_OUCH_HPP

#include <lazerbook/types.hpp>

#include <array>
#include <cstdint>

// NASDAQ OUCH 4.2 type layer. Structural only in v1: the field structs and
// message-type enums exist so the threading skeleton and a future io_uring net
// thread can compile against them, but no wire encode/decode is implemented yet.
namespace lazerbook::ouch {

enum class Inbound : char {
    EnterOrder = 'O',
    ReplaceOrder = 'U',
    CancelOrder = 'X',
    ModifyOrder = 'M',
};

enum class Outbound : char {
    Accepted = 'A',
    Replaced = 'U',
    Canceled = 'C',
    Executed = 'E',
    Rejected = 'J',
};

using Token = std::array<char, 14>;
using Firm = std::array<char, 4>;

enum class TimeInForce : std::uint8_t { Day, Ioc, GoodTillCancel };
enum class Display : char { Visible = 'Y', Hidden = 'N', Attributable = 'A' };
enum class Capacity : char { Agency = 'A', Principal = 'P', Riskless = 'R', Other = 'O' };
enum class CrossType : char { NoCross = 'N', OpeningCross = 'O', ClosingCross = 'C', Halt = 'H' };
enum class CustomerType : char { Retail = 'R', NotRetail = 'N' };

struct EnterOrder {
    Token token{};
    Side side{Side::Buy};
    std::uint32_t shares{};
    Symbol stock{};
    Price4 price{};
    TimeInForce time_in_force{TimeInForce::Day};
    Firm firm{};
    Display display{Display::Visible};
    Capacity capacity{Capacity::Agency};
    char intermarket_sweep{'N'};
    std::uint32_t min_qty{};
    CrossType cross_type{CrossType::NoCross};
    CustomerType customer_type{CustomerType::NotRetail};
};

struct CancelOrder {
    Token token{};
    std::uint32_t shares{};
};

struct ReplaceOrder {
    Token existing_token{};
    Token replacement_token{};
    std::uint32_t shares{};
    Price4 price{};
    TimeInForce time_in_force{TimeInForce::Day};
    Display display{Display::Visible};
    char intermarket_sweep{'N'};
    std::uint32_t min_qty{};
};

}  // namespace lazerbook::ouch

#endif  // LAZERBOOK_OUCH_HPP
