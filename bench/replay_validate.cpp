#include <lazerbook/bench/clock.hpp>
#include <lazerbook/book.hpp>
#include <lazerbook/framing.hpp>
#include <lazerbook/itch.hpp>
#include <lazerbook/itch_synth.hpp>
#include <lazerbook/itch_view.hpp>
#include <lazerbook/order_pool.hpp>
#include <lazerbook/reconstruct.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

// Replays a synthetic ITCH 5.0 stream through the real framing layer and
// checks the reconstructed book against the generator's ground truth.
//
// Previously this fed itch::parse a length obtained from the generator, so it
// exercised the decoder but never any framing -- the thing a real capture
// actually requires. It now builds a BinaryFILE-framed buffer (2-byte BE
// length prefix) and walks it with BinaryFileReader, the same path a Nasdaq
// day file takes.
//
// Runs the owning parser and the zero-copy view decoder over the identical
// stream so their throughput is directly comparable and their resulting book
// state can be asserted equal.
//
// Usage: replay_validate [events]

using namespace lazerbook;

namespace {

std::uint64_t side_shares(Book const& book, Side side) {
    std::uint64_t total = 0;
    std::uint32_t const lo = value_of(book.min_price());
    for (std::uint32_t i = 0; i < book.num_ticks(); ++i) {
        if (PriceLevel const* lvl = book.level_at(Price4{lo + i}, side)) {
            total += lvl->total_shares;
        }
    }
    return total;
}

struct Result {
    double secs = 0;
    std::uint64_t applied = 0;
    std::uint64_t bid_shares = 0;
    std::uint64_t ask_shares = 0;
    std::uint32_t best_bid = 0;
    std::uint32_t best_ask = 0;
    std::size_t live = 0;
};

Result replay(
    std::span<std::uint8_t const> framed, Price4 min_price, std::uint32_t num_ticks, bool zero_copy
) {
    Book book(min_price, num_ticks);
    OrderPool pool(1U << 20);
    Reconstructor recon(book, pool);

    Result r;
    BinaryFileReader reader{framed};
    std::uint64_t const t0 = bench::rdtsc_begin();
    while (true) {
        std::span<std::uint8_t const> const payload = reader.next();
        if (payload.empty()) {
            break;
        }
        if (zero_copy) {
            recon.apply_view_bytes(payload);
        } else {
            recon.apply_bytes(payload);
        }
        ++r.applied;
    }
    std::uint64_t const t1 = bench::rdtsc_end();

    r.secs = bench::ticks_to_ns(t1 - t0) / 1e9;
    r.bid_shares = side_shares(book, Side::Buy);
    r.ask_shares = side_shares(book, Side::Sell);
    r.best_bid = value_of(book.best_bid());
    r.best_ask = value_of(book.best_ask());
    r.live = recon.live_order_count();
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    std::uint64_t const n = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 5000000ULL;
    Price4 const min_price{50000};
    std::uint32_t const num_ticks = 1024;

    // Build the framed stream up front so neither timed run pays for
    // generation, and both see byte-identical input.
    itch::synth::Synth synth(0xBEEF, min_price, num_ticks);
    std::vector<std::uint8_t> framed;
    framed.reserve(static_cast<std::size_t>(n) * 34);
    std::array<std::uint8_t, 64> buf{};
    for (std::uint64_t i = 0; i < n; ++i) {
        std::size_t const len = synth.next(buf);
        std::array<std::uint8_t, 2> prefix{};
        write_be<std::uint16_t>(prefix.data(), static_cast<std::uint16_t>(len));
        framed.insert(framed.end(), prefix.begin(), prefix.end());
        framed.insert(framed.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(len));
    }
    auto const truth = synth.truth();

    Result const owning = replay(framed, min_price, num_ticks, /*zero_copy=*/false);
    Result const viewed = replay(framed, min_price, num_ticks, /*zero_copy=*/true);

    auto matches = [&truth](Result const& r) {
        return r.bid_shares == truth.total_bid_shares && r.ask_shares == truth.total_ask_shares &&
               r.best_bid == truth.best_bid && r.best_ask == truth.best_ask &&
               r.live == truth.bid_orders + truth.ask_orders;
    };
    bool const ok_owning = matches(owning);
    bool const ok_viewed = matches(viewed);
    // The two decoders must not merely both be fast -- they must agree.
    bool const agree = owning.bid_shares == viewed.bid_shares &&
                       owning.ask_shares == viewed.ask_shares &&
                       owning.best_bid == viewed.best_bid && owning.best_ask == viewed.best_ask &&
                       owning.live == viewed.live && owning.applied == viewed.applied;

    std::printf(
        "# lazerbook replay_validate: %llu events, BinaryFILE framed (%.2f MB)\n",
        static_cast<unsigned long long>(n), static_cast<double>(framed.size()) / (1024.0 * 1024.0)
    );
    std::printf(
        "owning parser    ground_truth=%s  %.2f M events/s\n", ok_owning ? "PASS" : "FAIL",
        static_cast<double>(owning.applied) / owning.secs / 1e6
    );
    std::printf(
        "zero-copy views  ground_truth=%s  %.2f M events/s  (%.2fx)\n", ok_viewed ? "PASS" : "FAIL",
        static_cast<double>(viewed.applied) / viewed.secs / 1e6, owning.secs / viewed.secs
    );
    std::printf("decoders_agree=%s\n", agree ? "PASS" : "FAIL");

    return (ok_owning && ok_viewed && agree) ? 0 : 1;
}
