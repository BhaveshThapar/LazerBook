#include <lazerbook/framing.hpp>
#include <lazerbook/itch_synth.hpp>
#include <lazerbook/itch_view.hpp>

#include <array>
#include <cstdint>
#include <doctest/doctest.h>
#include <variant>
#include <vector>

using namespace lazerbook;

namespace {

// Records what visit() dispatched to, and pulls every field out so the
// accessors are actually exercised rather than merely instantiated.
struct CapturingHandler {
    itch::MessageType last = itch::MessageType::None;
    std::uint64_t unhandled = 0;

    // Everything the reconstructor cares about, flattened.
    OrderId ref{};
    OrderId new_ref{};
    Side side{Side::Buy};
    std::uint32_t shares = 0;
    Price4 price{};
    Symbol stock{};
    std::uint64_t match_number = 0;
    Timestamp ts{};
    std::uint16_t locate = 0;

    void common(itch::MessageView const& v) {
        last = v.type();
        ts = v.timestamp();
        locate = v.stock_locate();
    }

    void on_message(itch::AddOrderView const& v) {
        common(v);
        ref = v.order_reference_number();
        side = v.buy_sell_indicator();
        shares = v.shares();
        stock = v.stock();
        price = v.price();
    }
    void on_message(itch::OrderExecutedView const& v) {
        common(v);
        ref = v.order_reference_number();
        shares = v.executed_shares();
        match_number = v.match_number();
    }
    void on_message(itch::OrderExecutedWithPriceView const& v) {
        common(v);
        ref = v.order_reference_number();
        shares = v.executed_shares();
        match_number = v.match_number();
        price = v.execution_price();
    }
    void on_message(itch::OrderCancelView const& v) {
        common(v);
        ref = v.order_reference_number();
        shares = v.cancelled_shares();
    }
    void on_message(itch::OrderDeleteView const& v) {
        common(v);
        ref = v.order_reference_number();
    }
    void on_message(itch::OrderReplaceView const& v) {
        common(v);
        ref = v.original_order_reference_number();
        new_ref = v.new_order_reference_number();
        shares = v.shares();
        price = v.price();
    }
    void on_message(itch::TradeView const& v) {
        common(v);
        ref = v.order_reference_number();
        side = v.buy_sell_indicator();
        shares = v.shares();
        stock = v.stock();
        price = v.price();
        match_number = v.match_number();
    }
    void on_message(itch::CrossTradeView const& v) {
        common(v);
        shares = static_cast<std::uint32_t>(v.shares());
        stock = v.stock();
        price = v.cross_price();
        match_number = v.match_number();
    }
    void on_message(itch::BrokenTradeView const& v) {
        common(v);
        match_number = v.match_number();
    }
    void on_message(itch::SystemEventView const& v) { common(v); }
    void on_message(itch::StockDirectoryView const& v) {
        common(v);
        stock = v.stock();
    }
    void on_message(itch::StockTradingActionView const& v) {
        common(v);
        stock = v.stock();
    }
    void on_message(itch::RegShoView const& v) {
        common(v);
        stock = v.stock();
    }
    void on_message(itch::NoiiView const& v) {
        common(v);
        stock = v.stock();
        price = v.current_reference_price();
    }
    void on_unhandled(itch::MessageView const& v) {
        common(v);
        ++unhandled;
    }
};

}  // namespace

TEST_CASE("itch view: rejects buffers shorter than the header") {
    std::array<std::uint8_t, 4> buf{};
    buf[0] = static_cast<std::uint8_t>('A');
    CapturingHandler h;
    CHECK(itch::visit(std::span<std::uint8_t const>(buf), h) == 0);
}

TEST_CASE("itch view: rejects unknown message types") {
    std::array<std::uint8_t, 64> buf{};
    buf[0] = static_cast<std::uint8_t>('%');
    CapturingHandler h;
    CHECK(itch::visit(std::span<std::uint8_t const>(buf), h) == 0);
}

TEST_CASE("itch view: rejects a known type with a truncated body") {
    std::array<std::uint8_t, 20> buf{};  // AddOrder needs 36
    buf[0] = static_cast<std::uint8_t>('A');
    CapturingHandler h;
    CHECK(itch::visit(std::span<std::uint8_t const>(buf), h) == 0);
}

TEST_CASE("itch view: returns the consumed length for each type") {
    // The length visit() reports is what a framing layer would advance by.
    std::array<std::uint8_t, 64> buf{};
    CapturingHandler h;
    struct Case {
        char type;
        std::size_t len;
    };
    for (Case c :
         {Case{'A', 36}, Case{'F', 40}, Case{'E', 31}, Case{'C', 36}, Case{'X', 23}, Case{'D', 19},
          Case{'U', 35}, Case{'P', 44}, Case{'Q', 40}, Case{'B', 19}, Case{'S', 12}, Case{'R', 39},
          Case{'H', 25}, Case{'Y', 20}, Case{'I', 50}}) {
        buf[0] = static_cast<std::uint8_t>(c.type);
        CHECK(itch::visit(std::span<std::uint8_t const>(buf), h) == c.len);
    }
}

TEST_CASE("itch view: shallow types reach the unhandled path") {
    std::array<std::uint8_t, 64> buf{};
    CapturingHandler h;
    for (char t : {'L', 'V', 'W', 'K', 'J', 'h', 'N'}) {
        buf[0] = static_cast<std::uint8_t>(t);
        CHECK(itch::visit(std::span<std::uint8_t const>(buf), h) > 0);
    }
    CHECK(h.unhandled == 7);
}

TEST_CASE("itch view: decodes identically to the owning parser") {
    // The differential that makes the zero-copy path safe to adopt: for every
    // message a real generator emits, the view accessors and itch::parse must
    // agree field for field.
    itch::synth::Synth synth(0xABCD, Price4{50000}, 1024);
    std::array<std::uint8_t, 64> buf{};

    std::uint64_t checked = 0;
    for (int i = 0; i < 200000; ++i) {
        std::size_t const len = synth.next(buf);
        std::span<std::uint8_t const> const bytes(buf.data(), len);

        auto owned = itch::parse(bytes);
        CapturingHandler h;
        std::size_t const consumed = itch::visit(bytes, h);
        REQUIRE(owned.has_value());
        REQUIRE(consumed == len);

        std::visit(
            [&](auto const& m) {
                using T = std::decay_t<decltype(m)>;
                // Header agrees for every type.
                REQUIRE(h.locate == m.hdr.stock_locate);
                REQUIRE(value_of(h.ts) == value_of(m.hdr.timestamp));
                REQUIRE(h.last == m.hdr.type);

                if constexpr (std::is_same_v<T, itch::AddOrder>) {
                    CHECK(value_of(h.ref) == value_of(m.order_reference_number));
                    CHECK(h.side == m.buy_sell_indicator);
                    CHECK(h.shares == m.shares);
                    CHECK(h.stock == m.stock);
                    CHECK(value_of(h.price) == value_of(m.price));
                } else if constexpr (std::is_same_v<T, itch::OrderExecuted>) {
                    CHECK(value_of(h.ref) == value_of(m.order_reference_number));
                    CHECK(h.shares == m.executed_shares);
                    CHECK(h.match_number == m.match_number);
                } else if constexpr (std::is_same_v<T, itch::OrderCancel>) {
                    CHECK(value_of(h.ref) == value_of(m.order_reference_number));
                    CHECK(h.shares == m.cancelled_shares);
                } else if constexpr (std::is_same_v<T, itch::OrderDelete>) {
                    CHECK(value_of(h.ref) == value_of(m.order_reference_number));
                } else if constexpr (std::is_same_v<T, itch::OrderReplace>) {
                    CHECK(value_of(h.ref) == value_of(m.original_order_reference_number));
                    CHECK(value_of(h.new_ref) == value_of(m.new_order_reference_number));
                    CHECK(h.shares == m.shares);
                    CHECK(value_of(h.price) == value_of(m.price));
                }
            },
            *owned
        );
        ++checked;
    }
    CHECK(checked == 200000);
}

// --- framing ----------------------------------------------------------------

namespace {

// Wraps each message in the BinaryFILE [2-byte BE length][payload] envelope.
std::vector<std::uint8_t> frame(std::vector<std::vector<std::uint8_t>> const& msgs) {
    std::vector<std::uint8_t> out;
    for (auto const& m : msgs) {
        std::array<std::uint8_t, 2> len{};
        write_be<std::uint16_t>(len.data(), static_cast<std::uint16_t>(m.size()));
        out.insert(out.end(), len.begin(), len.end());
        out.insert(out.end(), m.begin(), m.end());
    }
    return out;
}

std::vector<std::vector<std::uint8_t>> synth_messages(int n, std::uint64_t seed) {
    itch::synth::Synth synth(seed, Price4{50000}, 1024);
    std::array<std::uint8_t, 64> buf{};
    std::vector<std::vector<std::uint8_t>> out;
    for (int i = 0; i < n; ++i) {
        std::size_t const len = synth.next(buf);
        out.emplace_back(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(len));
    }
    return out;
}

}  // namespace

TEST_CASE("framing: empty input yields nothing") {
    BinaryFileReader r{{}};
    CHECK(r.next().empty());
    CHECK(r.done());
    CHECK(r.stats().messages == 0);
}

TEST_CASE("framing: walks a well-formed stream and recovers every message") {
    auto msgs = synth_messages(5000, 0x1234);
    auto bytes = frame(msgs);

    BinaryFileReader r{bytes};
    std::size_t i = 0;
    while (true) {
        auto payload = r.next();
        if (payload.empty()) {
            break;
        }
        REQUIRE(i < msgs.size());
        REQUIRE(payload.size() == msgs[i].size());
        CHECK(std::equal(payload.begin(), payload.end(), msgs[i].begin()));
        ++i;
    }
    CHECK(i == msgs.size());
    CHECK(r.stats().messages == msgs.size());
    CHECK(r.stats().truncated == 0);
    CHECK(r.stats().length_mismatch == 0);
    CHECK(r.done());
}

TEST_CASE("framing: a truncated payload is counted, not read past") {
    auto msgs = synth_messages(10, 0x99);
    auto bytes = frame(msgs);
    bytes.resize(bytes.size() - 3);  // cut into the last payload

    BinaryFileReader r{bytes};
    std::size_t seen = 0;
    while (!r.next().empty()) {
        ++seen;
    }
    CHECK(seen == 9);
    CHECK(r.stats().truncated == 1);
}

TEST_CASE("framing: a truncated length prefix is counted") {
    auto bytes = frame(synth_messages(4, 0x77));
    bytes.push_back(0x00);  // half a length prefix

    BinaryFileReader r{bytes};
    std::size_t seen = 0;
    while (!r.next().empty()) {
        ++seen;
    }
    CHECK(seen == 4);
    CHECK(r.stats().truncated == 1);
}

TEST_CASE("framing: a zero-length record stops the walk rather than spinning") {
    // Without an explicit guard this is an infinite loop: no bytes consumed,
    // same position, forever.
    std::vector<std::uint8_t> bytes{0x00, 0x00, 0x41, 0x42};
    BinaryFileReader r{bytes};
    CHECK(r.next().empty());
    CHECK(r.stats().zero_length == 1);
    CHECK(r.done());
}

TEST_CASE("framing: a framed length disagreeing with the spec is flagged") {
    // 'A' (AddOrder) is 36 bytes; claim 20.
    std::vector<std::uint8_t> bytes(22, 0);
    write_be<std::uint16_t>(bytes.data(), 20);
    bytes[2] = static_cast<std::uint8_t>('A');

    BinaryFileReader r{bytes};
    auto payload = r.next();
    CHECK(payload.size() == 20);
    CHECK(r.stats().length_mismatch == 1);
    CHECK(r.stats().messages == 1);
}

TEST_CASE("framing: framed stream feeds visit() end to end") {
    auto bytes = frame(synth_messages(20000, 0xBEEF));
    BinaryFileReader r{bytes};
    CapturingHandler h;
    std::uint64_t dispatched = 0;
    while (true) {
        auto payload = r.next();
        if (payload.empty()) {
            break;
        }
        // The framed length and the decoder's own idea of length must agree,
        // which is exactly what replay_validate could not check before.
        REQUIRE(itch::visit(payload, h) == payload.size());
        ++dispatched;
    }
    CHECK(dispatched == 20000);
    CHECK(r.stats().length_mismatch == 0);
}

TEST_CASE("mapped file: reports an error for a missing path") {
    MappedFile f;
    CHECK_FALSE(f.open("/nonexistent/lazerbook/should-not-exist.itch"));
    CHECK_FALSE(f.is_open());
    CHECK_FALSE(f.error().empty());
}
