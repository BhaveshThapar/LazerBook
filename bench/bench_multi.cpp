#include <lazerbook/bench/clock.hpp>
#include <lazerbook/bench/histogram.hpp>
#include <lazerbook/framing.hpp>
#include <lazerbook/multi_reconstruct.hpp>
#include <lazerbook/types.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// Multi-instrument reconstruction at Nasdaq-day scale.
//
// Reports the two things that decide whether a full-day replay is feasible:
// the resident ladder footprint across ~9500 instruments, and sustained
// messages/sec through framing, zero-copy decode and book update.
//
// Usage: bench_multi [--instruments N] [--messages N] [--live N]

using namespace lazerbook;

namespace {

struct Args {
    std::uint32_t instruments = 9500;
    std::uint64_t messages = 20'000'000;
    std::uint64_t target_live = 3'000'000;
};

// Builds a BinaryFILE-framed multi-instrument stream up front so the timed
// region measures only decode and book update.
std::vector<std::uint8_t> build_stream(Args const& a, std::uint64_t& adds) {
    std::vector<std::uint8_t> out;
    out.reserve(static_cast<std::size_t>(a.messages) * 30);

    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    auto rng = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };

    std::vector<std::uint64_t> live;
    live.reserve(static_cast<std::size_t>(a.target_live) + 1024);
    std::uint64_t next_ref = 1;
    std::uint64_t ts = 0;
    adds = 0;

    auto emit = [&out](std::uint8_t const* p, std::size_t len) {
        std::uint8_t prefix[2];
        write_be<std::uint16_t>(prefix, static_cast<std::uint16_t>(len));
        out.insert(out.end(), prefix, prefix + 2);
        out.insert(out.end(), p, p + len);
    };

    for (std::uint64_t i = 0; i < a.messages; ++i) {
        std::uint64_t const q = rng();
        // Grow to the target live-order count, then hold it steady.
        bool const add = live.size() < a.target_live || (q % 100) < 50;

        std::uint8_t m[40] = {};
        write_be<std::uint16_t>(m + 3, 0);
        write_be48(m + 5, ts++);

        if (add) {
            auto const locate = static_cast<std::uint16_t>(1 + (rng() % a.instruments));
            // Instruments sit anywhere from ~$10 to ~$200. Quotes stay within
            // a dollar of their own level so they land inside the window the
            // first message established -- a wider spread would just measure
            // the out-of-range counter.
            std::uint32_t const base = 100000U + (static_cast<std::uint32_t>(locate) * 200U);
            std::uint32_t const price = base + (static_cast<std::uint32_t>(rng() % 100) * 100U);
            std::uint64_t const ref = next_ref++;

            m[0] = static_cast<std::uint8_t>('A');
            write_be<std::uint16_t>(m + 1, locate);
            write_be<std::uint64_t>(m + 11, ref);
            m[19] = static_cast<std::uint8_t>((q & 1U) ? Side::Buy : Side::Sell);
            write_be<std::uint32_t>(m + 20, 1U + static_cast<std::uint32_t>(rng() % 500));
            std::memset(m + 24, ' ', 8);
            write_be<Price4>(m + 32, Price4{price});
            emit(m, 36);
            live.push_back(ref);
            ++adds;
        } else {
            std::size_t const at = static_cast<std::size_t>(rng()) % live.size();
            std::uint64_t const ref = live[at];
            live[at] = live.back();
            live.pop_back();

            m[0] = static_cast<std::uint8_t>('D');
            write_be<std::uint16_t>(m + 1, 0);
            write_be<std::uint64_t>(m + 11, ref);
            emit(m, 19);
        }
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--instruments") == 0 && i + 1 < argc) {
            a.instruments = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--messages") == 0 && i + 1 < argc) {
            a.messages = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--live") == 0 && i + 1 < argc) {
            a.target_live = std::strtoull(argv[++i], nullptr, 10);
        } else {
            std::fprintf(
                stderr, "usage: %s [--instruments N] [--messages N] [--live N]\n", argv[0]
            );
            return 2;
        }
    }

    std::printf(
        "# lazerbook bench_multi: %u instruments, %llu messages, %llu target live\n", a.instruments,
        static_cast<unsigned long long>(a.messages), static_cast<unsigned long long>(a.target_live)
    );

    std::uint64_t adds = 0;
    std::vector<std::uint8_t> const stream = build_stream(a, adds);
    std::printf(
        "stream: %.2f MB framed, %llu adds\n",
        static_cast<double>(stream.size()) / (1024.0 * 1024.0),
        static_cast<unsigned long long>(adds)
    );

    MultiReconstructor recon(
        static_cast<std::uint16_t>(a.instruments),
        static_cast<std::size_t>(a.target_live) + (1U << 20)
    );

    BinaryFileReader reader{stream};
    std::uint64_t applied = 0;
    std::uint64_t const t0 = bench::rdtsc_begin();
    while (true) {
        std::span<std::uint8_t const> const payload = reader.next();
        if (payload.empty()) {
            break;
        }
        recon.apply(payload);
        ++applied;
    }
    std::uint64_t const t1 = bench::rdtsc_end();

    double const secs = bench::ticks_to_ns(t1 - t0) / 1e9;
    auto const& s = recon.stats();

    std::printf(
        "applied=%llu  %.2f M msg/s  (%.1f ns/msg)\n", static_cast<unsigned long long>(applied),
        static_cast<double>(applied) / secs / 1e6, secs * 1e9 / static_cast<double>(applied)
    );
    std::printf(
        "books=%zu  ladder=%.1f MB  live_orders=%zu\n", recon.registry().book_count(),
        static_cast<double>(recon.registry().ladder_bytes()) / (1024.0 * 1024.0),
        recon.live_order_count()
    );
    std::printf(
        "added=%llu deleted=%llu  skip_oor=%llu skip_unknown_ref=%llu skip_no_book=%llu\n",
        static_cast<unsigned long long>(s.added), static_cast<unsigned long long>(s.deleted),
        static_cast<unsigned long long>(s.skip_oor),
        static_cast<unsigned long long>(s.skip_unknown_ref),
        static_cast<unsigned long long>(s.skip_no_book)
    );

    // A run that silently dropped everything would otherwise look fast.
    return (s.skip_oor == 0 && s.skip_unknown_ref == 0 && s.skip_no_book == 0) ? 0 : 1;
}
