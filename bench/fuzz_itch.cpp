#include <lazerbook/itch.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

// libFuzzer entry point: feed arbitrary bytes to the ITCH parser and discard the
// result. The parser must never crash or trip a sanitizer on any input.
extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size) {
    auto result = lazerbook::itch::parse(std::span<std::uint8_t const>(data, size));
    if (result.has_value()) {
        // Touch the variant so the optimiser cannot elide the parse.
        volatile auto index = result->index();
        (void)index;
    }
    return 0;
}
