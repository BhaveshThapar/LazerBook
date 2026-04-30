#include <lazerbook/itch.hpp>

#include <algorithm>

namespace lazerbook::itch {
namespace {

// Body always begins at offset 11 (1 type + 2 locate + 2 tracking + 6 ts).
constexpr std::size_t kHeaderLen = 11;

Header read_header(std::uint8_t const* p) noexcept {
    Header h;
    h.type = static_cast<MessageType>(static_cast<char>(p[0]));
    h.stock_locate = read_be<std::uint16_t>(p + 1);
    h.tracking_number = read_be<std::uint16_t>(p + 3);
    h.timestamp = Timestamp{read_be48(p + 5)};
    return h;
}

}  // namespace

std::size_t expected_length(MessageType type) noexcept {
    switch (type) {
        case MessageType::SystemEvent:
            return 12;
        case MessageType::StockDirectory:
            return 39;
        case MessageType::StockTradingAction:
            return 25;
        case MessageType::RegSho:
            return 20;
        case MessageType::MarketParticipantPosition:
            return 26;
        case MessageType::MwcbDeclineLevel:
            return 35;
        case MessageType::MwcbStatus:
            return 12;
        case MessageType::IpoQuotingPeriodUpdate:
            return 28;
        case MessageType::LuldAuctionCollar:
            return 35;
        case MessageType::OperationalHalt:
            return 21;
        case MessageType::AddOrder:
            return 36;
        case MessageType::AddOrderMpid:
            return 40;
        case MessageType::OrderExecuted:
            return 31;
        case MessageType::OrderExecutedWithPrice:
            return 36;
        case MessageType::OrderCancel:
            return 23;
        case MessageType::OrderDelete:
            return 19;
        case MessageType::OrderReplace:
            return 36;
        case MessageType::Trade:
            return 44;
        case MessageType::CrossTrade:
            return 40;
        case MessageType::BrokenTrade:
            return 19;
        case MessageType::Noii:
            return 50;
        case MessageType::Rpii:
            return 20;
        case MessageType::None:
            return 0;
    }
    return 0;
}

std::expected<Message, ParseError> parse(std::span<std::uint8_t const> buf) noexcept {
    if (buf.size() < kHeaderLen) {
        return std::unexpected(ParseError::BufferTooShort);
    }
    std::uint8_t const* p = buf.data();
    auto const type = static_cast<MessageType>(static_cast<char>(p[0]));
    std::size_t const need = expected_length(type);
    if (need == 0) {
        return std::unexpected(ParseError::UnknownMessageType);
    }
    if (buf.size() < need) {
        return std::unexpected(ParseError::BufferTooShort);
    }
    Header const hdr = read_header(p);

    switch (type) {
        case MessageType::SystemEvent: {
            SystemEvent m;
            m.hdr = hdr;
            m.event_code = static_cast<char>(p[11]);
            return m;
        }
        case MessageType::StockDirectory: {
            StockDirectory m;
            m.hdr = hdr;
            m.stock = read_symbol(p + 11);
            m.market_category = static_cast<char>(p[19]);
            m.financial_status_indicator = static_cast<char>(p[20]);
            m.round_lot_size = read_be<std::uint32_t>(p + 21);
            m.round_lots_only = static_cast<char>(p[25]);
            m.issue_classification = static_cast<char>(p[26]);
            m.issue_subtype = {static_cast<char>(p[27]), static_cast<char>(p[28])};
            m.authenticity = static_cast<char>(p[29]);
            m.short_sale_threshold_indicator = static_cast<char>(p[30]);
            m.ipo_flag = static_cast<char>(p[31]);
            m.luld_reference_price_tier = static_cast<char>(p[32]);
            m.etp_flag = static_cast<char>(p[33]);
            m.etp_leverage_factor = read_be<std::uint32_t>(p + 34);
            m.inverse_indicator = static_cast<char>(p[38]);
            return m;
        }
        case MessageType::StockTradingAction: {
            StockTradingAction m;
            m.hdr = hdr;
            m.stock = read_symbol(p + 11);
            m.trading_state = static_cast<char>(p[19]);
            m.reserved = static_cast<char>(p[20]);
            m.reason = {
                static_cast<char>(p[21]), static_cast<char>(p[22]), static_cast<char>(p[23]),
                static_cast<char>(p[24])
            };
            return m;
        }
        case MessageType::RegSho: {
            RegSho m;
            m.hdr = hdr;
            m.stock = read_symbol(p + 11);
            m.reg_sho_action = static_cast<char>(p[19]);
            return m;
        }
        case MessageType::AddOrder: {
            AddOrder m;
            m.hdr = hdr;
            m.order_reference_number = OrderId{read_be<std::uint64_t>(p + 11)};
            m.buy_sell_indicator = static_cast<Side>(static_cast<char>(p[19]));
            m.shares = read_be<std::uint32_t>(p + 20);
            m.stock = read_symbol(p + 24);
            m.price = read_be<Price4>(p + 32);
            return m;
        }
        case MessageType::AddOrderMpid: {
            AddOrderMpid m;
            m.hdr = hdr;
            m.order_reference_number = OrderId{read_be<std::uint64_t>(p + 11)};
            m.buy_sell_indicator = static_cast<Side>(static_cast<char>(p[19]));
            m.shares = read_be<std::uint32_t>(p + 20);
            m.stock = read_symbol(p + 24);
            m.price = read_be<Price4>(p + 32);
            m.attribution = {
                static_cast<char>(p[36]), static_cast<char>(p[37]), static_cast<char>(p[38]),
                static_cast<char>(p[39])
            };
            return m;
        }
        case MessageType::OrderExecuted: {
            OrderExecuted m;
            m.hdr = hdr;
            m.order_reference_number = OrderId{read_be<std::uint64_t>(p + 11)};
            m.executed_shares = read_be<std::uint32_t>(p + 19);
            m.match_number = read_be<std::uint64_t>(p + 23);
            return m;
        }
        case MessageType::OrderExecutedWithPrice: {
            OrderExecutedWithPrice m;
            m.hdr = hdr;
            m.order_reference_number = OrderId{read_be<std::uint64_t>(p + 11)};
            m.executed_shares = read_be<std::uint32_t>(p + 19);
            m.match_number = read_be<std::uint64_t>(p + 23);
            m.printable = static_cast<char>(p[31]);
            m.execution_price = read_be<Price4>(p + 32);
            return m;
        }
        case MessageType::OrderCancel: {
            OrderCancel m;
            m.hdr = hdr;
            m.order_reference_number = OrderId{read_be<std::uint64_t>(p + 11)};
            m.cancelled_shares = read_be<std::uint32_t>(p + 19);
            return m;
        }
        case MessageType::OrderDelete: {
            OrderDelete m;
            m.hdr = hdr;
            m.order_reference_number = OrderId{read_be<std::uint64_t>(p + 11)};
            return m;
        }
        case MessageType::OrderReplace: {
            OrderReplace m;
            m.hdr = hdr;
            m.original_order_reference_number = OrderId{read_be<std::uint64_t>(p + 11)};
            m.new_order_reference_number = OrderId{read_be<std::uint64_t>(p + 19)};
            m.shares = read_be<std::uint32_t>(p + 27);
            m.price = read_be<Price4>(p + 31);
            return m;
        }
        case MessageType::Trade: {
            Trade m;
            m.hdr = hdr;
            m.order_reference_number = OrderId{read_be<std::uint64_t>(p + 11)};
            m.buy_sell_indicator = static_cast<Side>(static_cast<char>(p[19]));
            m.shares = read_be<std::uint32_t>(p + 20);
            m.stock = read_symbol(p + 24);
            m.price = read_be<Price4>(p + 32);
            m.match_number = read_be<std::uint64_t>(p + 36);
            return m;
        }
        case MessageType::CrossTrade: {
            CrossTrade m;
            m.hdr = hdr;
            m.shares = read_be<std::uint64_t>(p + 11);
            m.stock = read_symbol(p + 19);
            m.cross_price = read_be<Price4>(p + 27);
            m.match_number = read_be<std::uint64_t>(p + 31);
            m.cross_type = static_cast<char>(p[39]);
            return m;
        }
        case MessageType::BrokenTrade: {
            BrokenTrade m;
            m.hdr = hdr;
            m.match_number = read_be<std::uint64_t>(p + 11);
            return m;
        }
        case MessageType::Noii: {
            Noii m;
            m.hdr = hdr;
            m.paired_shares = read_be<std::uint64_t>(p + 11);
            m.imbalance_shares = read_be<std::uint64_t>(p + 19);
            m.imbalance_direction = static_cast<char>(p[27]);
            m.stock = read_symbol(p + 28);
            m.far_price = read_be<Price4>(p + 36);
            m.near_price = read_be<Price4>(p + 40);
            m.current_reference_price = read_be<Price4>(p + 44);
            m.cross_type = static_cast<char>(p[48]);
            m.price_variation_indicator = static_cast<char>(p[49]);
            return m;
        }
        // Recognised-but-shallow types: stash the body bytes into Unknown.
        case MessageType::MarketParticipantPosition:
        case MessageType::MwcbDeclineLevel:
        case MessageType::MwcbStatus:
        case MessageType::IpoQuotingPeriodUpdate:
        case MessageType::LuldAuctionCollar:
        case MessageType::OperationalHalt:
        case MessageType::Rpii: {
            Unknown m;
            m.hdr = hdr;
            std::size_t const body = need - kHeaderLen;
            std::size_t const n = std::min(body, m.raw.size());
            std::copy_n(p + kHeaderLen, n, m.raw.begin());
            m.length = static_cast<std::uint8_t>(n);
            return m;
        }
        case MessageType::None:
            break;
    }
    return std::unexpected(ParseError::UnknownMessageType);
}

}  // namespace lazerbook::itch
