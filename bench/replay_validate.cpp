#include <lazerbook/bench/clock.hpp>
#include <lazerbook/book.hpp>
#include <lazerbook/itch.hpp>
#include <lazerbook/itch_synth.hpp>
#include <lazerbook/order_pool.hpp>
#include <lazerbook/reconstruct.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>

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

}  // namespace

int main(int argc, char** argv) {
    std::uint64_t const n = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 5000000ULL;
    Price4 const min_price{50000};
    std::uint32_t const num_ticks = 1024;

    itch::synth::Synth synth(0xBEEF, min_price, num_ticks);
    Book book(min_price, num_ticks);
    OrderPool pool(1U << 20);
    Reconstructor recon(book, pool);

    std::array<std::uint8_t, 64> buf{};
    std::uint64_t const t0 = bench::now_ticks();
    for (std::uint64_t i = 0; i < n; ++i) {
        std::size_t const len = synth.next(buf);
        auto parsed = itch::parse(std::span<std::uint8_t const>(buf.data(), len));
        if (parsed.has_value()) {
            recon.apply(*parsed);
        }
    }
    std::uint64_t const t1 = bench::now_ticks();

    auto const truth = synth.truth();
    bool const ok = side_shares(book, Side::Buy) == truth.total_bid_shares &&
                    side_shares(book, Side::Sell) == truth.total_ask_shares &&
                    value_of(book.best_bid()) == truth.best_bid &&
                    value_of(book.best_ask()) == truth.best_ask &&
                    recon.live_order_count() == truth.bid_orders + truth.ask_orders;

    double const secs = bench::ticks_to_ns(t1 - t0) / 1e9;
    std::printf("# lazerbook replay_validate: %llu events\n", static_cast<unsigned long long>(n));
    std::printf(
        "ground_truth_match=%s  throughput=%.2f M events/s\n", ok ? "PASS" : "FAIL",
        static_cast<double>(n) / secs / 1e6
    );
    return ok ? 0 : 1;
}
