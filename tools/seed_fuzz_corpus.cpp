#include <lazerbook/itch_synth.hpp>
#include <lazerbook/types.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>

// Emits synth ITCH messages as individual seed files for libFuzzer to mutate.
// Usage: seed_fuzz_corpus <corpus-dir> [count]
using namespace lazerbook;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <corpus-dir> [count]\n", argv[0]);
        return 1;
    }
    std::string const dir = argv[1];
    int const count = (argc > 2) ? std::atoi(argv[2]) : 1024;

    itch::synth::Synth synth(0x533D, Price4{50000}, 1024);
    std::array<std::uint8_t, 64> buf{};
    for (int i = 0; i < count; ++i) {
        std::size_t const n = synth.next(buf);
        if (n == 0) {
            continue;
        }
        char path[512];
        std::snprintf(path, sizeof(path), "%s/seed_%05d.bin", dir.c_str(), i);
        std::FILE* f = std::fopen(path, "wb");
        if (f == nullptr) {
            std::fprintf(stderr, "cannot open %s\n", path);
            return 1;
        }
        std::fwrite(buf.data(), 1, n, f);
        std::fclose(f);
    }
    std::printf("wrote %d seeds to %s\n", count, dir.c_str());
    return 0;
}
