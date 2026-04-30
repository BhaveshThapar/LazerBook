#include <lazerbook/itch.hpp>
#include <lazerbook/types.hpp>

#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <span>
#include <string_view>
#include <vector>

using namespace lazerbook;
using namespace lazerbook::itch;

namespace {

void put_header(
    std::uint8_t* p, char type, std::uint16_t locate, std::uint16_t track, std::uint64_t ts
) {
    p[0] = static_cast<std::uint8_t>(type);
    write_be<std::uint16_t>(p + 1, locate);
    write_be<std::uint16_t>(p + 3, track);
    write_be48(p + 5, ts);
}

Symbol sym(std::string_view s) {
    Symbol out;
    out.bytes.fill(' ');
    for (std::size_t i = 0; i < s.size() && i < out.bytes.size(); ++i) {
        out.bytes[i] = s[i];
    }
    return out;
}

}  // namespace

TEST_CASE("parse rejects buffers shorter than the header") {
    std::array<std::uint8_t, 5> buf{'A', 0, 0, 0, 0};
    auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == ParseError::BufferTooShort);
}

TEST_CASE("parse rejects unknown message types") {
    std::array<std::uint8_t, 16> buf{};
    put_header(buf.data(), 'z', 1, 2, 3);
    auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == ParseError::UnknownMessageType);
}

TEST_CASE("parse rejects a known type with a truncated body") {
    std::array<std::uint8_t, 20> buf{};
    put_header(buf.data(), 'A', 1, 2, 3);  // AddOrder needs 36
    auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == ParseError::BufferTooShort);
}

TEST_CASE("SystemEvent round-trips") {
    std::array<std::uint8_t, 12> buf{};
    put_header(buf.data(), 'S', 0, 0, 42);
    buf[11] = 'O';
    auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
    REQUIRE(r.has_value());
    auto const& m = std::get<SystemEvent>(*r);
    CHECK(m.hdr.type == MessageType::SystemEvent);
    CHECK(value_of(m.hdr.timestamp) == 42U);
    CHECK(m.event_code == 'O');
}

TEST_CASE("AddOrder round-trips all fields") {
    std::array<std::uint8_t, 36> buf{};
    put_header(buf.data(), 'A', 7, 9, 123456);
    write_be<std::uint64_t>(buf.data() + 11, 1001);
    buf[19] = static_cast<std::uint8_t>('B');
    write_be<std::uint32_t>(buf.data() + 20, 500);
    write_symbol(buf.data() + 24, sym("AAPL"));
    write_be<Price4>(buf.data() + 32, Price4{1500000});
    auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
    REQUIRE(r.has_value());
    auto const& m = std::get<AddOrder>(*r);
    CHECK(value_of(m.order_reference_number) == 1001U);
    CHECK(m.buy_sell_indicator == Side::Buy);
    CHECK(m.shares == 500U);
    CHECK(m.stock.view() == "AAPL");
    CHECK(value_of(m.price) == 1500000U);
    CHECK(m.hdr.stock_locate == 7U);
}

TEST_CASE("AddOrderMpid round-trips attribution") {
    std::array<std::uint8_t, 40> buf{};
    put_header(buf.data(), 'F', 0, 0, 1);
    write_be<std::uint64_t>(buf.data() + 11, 2002);
    buf[19] = static_cast<std::uint8_t>('S');
    write_be<std::uint32_t>(buf.data() + 20, 100);
    write_symbol(buf.data() + 24, sym("MSFT"));
    write_be<Price4>(buf.data() + 32, Price4{2500000});
    buf[36] = 'F';
    buf[37] = 'I';
    buf[38] = 'R';
    buf[39] = 'M';
    auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
    REQUIRE(r.has_value());
    auto const& m = std::get<AddOrderMpid>(*r);
    CHECK(m.buy_sell_indicator == Side::Sell);
    CHECK(m.attribution[0] == 'F');
    CHECK(m.attribution[3] == 'M');
}

TEST_CASE("OrderExecuted and OrderExecutedWithPrice round-trip") {
    {
        std::array<std::uint8_t, 31> buf{};
        put_header(buf.data(), 'E', 0, 0, 1);
        write_be<std::uint64_t>(buf.data() + 11, 1001);
        write_be<std::uint32_t>(buf.data() + 19, 250);
        write_be<std::uint64_t>(buf.data() + 23, 9999);
        auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
        REQUIRE(r.has_value());
        auto const& m = std::get<OrderExecuted>(*r);
        CHECK(m.executed_shares == 250U);
        CHECK(m.match_number == 9999U);
    }
    {
        std::array<std::uint8_t, 36> buf{};
        put_header(buf.data(), 'C', 0, 0, 1);
        write_be<std::uint64_t>(buf.data() + 11, 1001);
        write_be<std::uint32_t>(buf.data() + 19, 250);
        write_be<std::uint64_t>(buf.data() + 23, 9999);
        buf[31] = 'Y';
        write_be<Price4>(buf.data() + 32, Price4{1499000});
        auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
        REQUIRE(r.has_value());
        auto const& m = std::get<OrderExecutedWithPrice>(*r);
        CHECK(m.printable == 'Y');
        CHECK(value_of(m.execution_price) == 1499000U);
    }
}

TEST_CASE("OrderCancel and OrderDelete round-trip") {
    {
        std::array<std::uint8_t, 23> buf{};
        put_header(buf.data(), 'X', 0, 0, 1);
        write_be<std::uint64_t>(buf.data() + 11, 1001);
        write_be<std::uint32_t>(buf.data() + 19, 75);
        auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
        REQUIRE(r.has_value());
        CHECK(std::get<OrderCancel>(*r).cancelled_shares == 75U);
    }
    {
        std::array<std::uint8_t, 19> buf{};
        put_header(buf.data(), 'D', 0, 0, 1);
        write_be<std::uint64_t>(buf.data() + 11, 1001);
        auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
        REQUIRE(r.has_value());
        CHECK(value_of(std::get<OrderDelete>(*r).order_reference_number) == 1001U);
    }
}

TEST_CASE("OrderReplace round-trips both refs") {
    std::array<std::uint8_t, 35> buf{};
    put_header(buf.data(), 'U', 0, 0, 1);
    write_be<std::uint64_t>(buf.data() + 11, 1001);
    write_be<std::uint64_t>(buf.data() + 19, 1002);
    write_be<std::uint32_t>(buf.data() + 27, 300);
    write_be<Price4>(buf.data() + 31, Price4{1510000});
    auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
    REQUIRE(r.has_value());
    auto const& m = std::get<OrderReplace>(*r);
    CHECK(value_of(m.original_order_reference_number) == 1001U);
    CHECK(value_of(m.new_order_reference_number) == 1002U);
    CHECK(m.shares == 300U);
    CHECK(value_of(m.price) == 1510000U);
}

TEST_CASE("Trade, CrossTrade, BrokenTrade round-trip") {
    {
        std::array<std::uint8_t, 44> buf{};
        put_header(buf.data(), 'P', 0, 0, 1);
        write_be<std::uint64_t>(buf.data() + 11, 1);
        buf[19] = static_cast<std::uint8_t>('B');
        write_be<std::uint32_t>(buf.data() + 20, 400);
        write_symbol(buf.data() + 24, sym("NVDA"));
        write_be<Price4>(buf.data() + 32, Price4{9000000});
        write_be<std::uint64_t>(buf.data() + 36, 555);
        auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
        REQUIRE(r.has_value());
        auto const& m = std::get<Trade>(*r);
        CHECK(m.shares == 400U);
        CHECK(m.match_number == 555U);
        CHECK(m.stock.view() == "NVDA");
    }
    {
        std::array<std::uint8_t, 40> buf{};
        put_header(buf.data(), 'Q', 0, 0, 1);
        write_be<std::uint64_t>(buf.data() + 11, 100000);
        write_symbol(buf.data() + 19, sym("SPY"));
        write_be<Price4>(buf.data() + 27, Price4{4500000});
        write_be<std::uint64_t>(buf.data() + 31, 777);
        buf[39] = 'O';
        auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
        REQUIRE(r.has_value());
        auto const& m = std::get<CrossTrade>(*r);
        CHECK(m.shares == 100000U);
        CHECK(m.cross_type == 'O');
    }
    {
        std::array<std::uint8_t, 19> buf{};
        put_header(buf.data(), 'B', 0, 0, 1);
        write_be<std::uint64_t>(buf.data() + 11, 888);
        auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
        REQUIRE(r.has_value());
        CHECK(std::get<BrokenTrade>(*r).match_number == 888U);
    }
}

TEST_CASE("Noii round-trips imbalance fields") {
    std::array<std::uint8_t, 50> buf{};
    put_header(buf.data(), 'I', 0, 0, 1);
    write_be<std::uint64_t>(buf.data() + 11, 1000);
    write_be<std::uint64_t>(buf.data() + 19, 200);
    buf[27] = 'B';
    write_symbol(buf.data() + 28, sym("QQQ"));
    write_be<Price4>(buf.data() + 36, Price4{3000000});
    write_be<Price4>(buf.data() + 40, Price4{3001000});
    write_be<Price4>(buf.data() + 44, Price4{3000500});
    buf[48] = 'O';
    buf[49] = 'L';
    auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
    REQUIRE(r.has_value());
    auto const& m = std::get<Noii>(*r);
    CHECK(m.paired_shares == 1000U);
    CHECK(m.imbalance_shares == 200U);
    CHECK(m.imbalance_direction == 'B');
    CHECK(value_of(m.current_reference_price) == 3000500U);
}

TEST_CASE("StockDirectory round-trips its fields") {
    std::array<std::uint8_t, 39> buf{};
    put_header(buf.data(), 'R', 11, 0, 1);
    write_symbol(buf.data() + 11, sym("AAPL"));
    buf[19] = 'Q';  // market_category
    buf[20] = 'N';  // financial_status
    write_be<std::uint32_t>(buf.data() + 21, 100);
    buf[25] = 'N';
    buf[26] = 'C';
    buf[27] = ' ';
    buf[28] = ' ';
    buf[29] = 'P';
    buf[30] = 'N';
    buf[31] = 'N';
    buf[32] = '1';
    buf[33] = 'N';
    write_be<std::uint32_t>(buf.data() + 34, 0);
    buf[38] = 'N';
    auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
    REQUIRE(r.has_value());
    auto const& m = std::get<StockDirectory>(*r);
    CHECK(m.stock.view() == "AAPL");
    CHECK(m.market_category == 'Q');
    CHECK(m.round_lot_size == 100U);
    CHECK(m.luld_reference_price_tier == '1');
}

TEST_CASE("StockTradingAction round-trips trading state and reason") {
    std::array<std::uint8_t, 25> buf{};
    put_header(buf.data(), 'H', 0, 0, 1);
    write_symbol(buf.data() + 11, sym("MSFT"));
    buf[19] = 'T';  // trading_state
    buf[20] = ' ';  // reserved
    buf[21] = 'T';
    buf[22] = '1';
    buf[23] = ' ';
    buf[24] = ' ';
    auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
    REQUIRE(r.has_value());
    auto const& m = std::get<StockTradingAction>(*r);
    CHECK(m.stock.view() == "MSFT");
    CHECK(m.trading_state == 'T');
    CHECK(m.reason[0] == 'T');
    CHECK(m.reason[1] == '1');
}

TEST_CASE("RegSho round-trips the action code") {
    std::array<std::uint8_t, 20> buf{};
    put_header(buf.data(), 'Y', 0, 0, 1);
    write_symbol(buf.data() + 11, sym("NVDA"));
    buf[19] = '1';
    auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
    REQUIRE(r.has_value());
    auto const& m = std::get<RegSho>(*r);
    CHECK(m.stock.view() == "NVDA");
    CHECK(m.reg_sho_action == '1');
}

TEST_CASE("recognised-but-shallow types land in the Unknown arm") {
    std::array<std::uint8_t, 26> buf{};  // 'L' MarketParticipantPosition
    put_header(buf.data(), 'L', 3, 4, 5);
    buf[11] = 0xAB;
    auto r = parse(std::span<std::uint8_t const>(buf.data(), buf.size()));
    REQUIRE(r.has_value());
    auto const& m = std::get<Unknown>(*r);
    CHECK(m.hdr.type == MessageType::MarketParticipantPosition);
    CHECK(m.length == 26 - 11);
    CHECK(m.raw[0] == 0xAB);
}

TEST_CASE("expected_length matches the spec table") {
    CHECK(expected_length(MessageType::SystemEvent) == 12);
    CHECK(expected_length(MessageType::AddOrder) == 36);
    CHECK(expected_length(MessageType::AddOrderMpid) == 40);
    CHECK(expected_length(MessageType::OrderReplace) == 35);
    CHECK(expected_length(MessageType::Trade) == 44);
    CHECK(expected_length(MessageType::Noii) == 50);
    CHECK(expected_length(MessageType::None) == 0);
}
