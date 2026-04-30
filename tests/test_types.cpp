#include <lazerbook/types.hpp>

#include <array>
#include <cstdint>
#include <doctest/doctest.h>

using namespace lazerbook;

TEST_CASE("read_be decodes big-endian integers") {
    std::array<std::uint8_t, 8> buf{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    CHECK(read_be<std::uint16_t>(buf.data()) == 0x0102);
    CHECK(read_be<std::uint32_t>(buf.data()) == 0x01020304U);
    CHECK(read_be<std::uint64_t>(buf.data()) == 0x0102030405060708ULL);
}

TEST_CASE("read_be works on strong enum types") {
    std::array<std::uint8_t, 4> buf{0x00, 0x00, 0x27, 0x10};  // 10000
    CHECK(value_of(read_be<Price4>(buf.data())) == 10000U);
}

TEST_CASE("read_be48 decodes 6-byte big-endian") {
    std::array<std::uint8_t, 6> buf{0x00, 0x00, 0x00, 0x00, 0x01, 0x00};
    CHECK(read_be48(buf.data()) == 256U);
    std::array<std::uint8_t, 6> max{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    CHECK(read_be48(max.data()) == 0xFFFFFFFFFFFFULL);
}

TEST_CASE("read_symbol trims trailing spaces in view") {
    std::array<std::uint8_t, 8> buf{'A', 'A', 'P', 'L', ' ', ' ', ' ', ' '};
    Symbol s = read_symbol(buf.data());
    CHECK(s.view() == "AAPL");
}

TEST_CASE("write_be round-trips with read_be") {
    std::array<std::uint8_t, 8> buf{};
    write_be<std::uint16_t>(buf.data(), 0xBEEF);
    CHECK(read_be<std::uint16_t>(buf.data()) == 0xBEEF);

    write_be<std::uint32_t>(buf.data(), 0xDEADBEEFU);
    CHECK(read_be<std::uint32_t>(buf.data()) == 0xDEADBEEFU);

    write_be<std::uint64_t>(buf.data(), 0x0123456789ABCDEFULL);
    CHECK(read_be<std::uint64_t>(buf.data()) == 0x0123456789ABCDEFULL);
}

TEST_CASE("write_be encodes most-significant byte first") {
    std::array<std::uint8_t, 4> buf{};
    write_be<std::uint32_t>(buf.data(), 0x01020304U);
    CHECK(buf[0] == 0x01);
    CHECK(buf[1] == 0x02);
    CHECK(buf[2] == 0x03);
    CHECK(buf[3] == 0x04);
}

TEST_CASE("write_be48 round-trips with read_be48") {
    std::array<std::uint8_t, 6> buf{};
    write_be48(buf.data(), 0x0000ABCDEF12ULL);
    CHECK(read_be48(buf.data()) == 0x0000ABCDEF12ULL);
}

TEST_CASE("write_be on Price4 round-trips") {
    std::array<std::uint8_t, 4> buf{};
    write_be<Price4>(buf.data(), Price4{123456});
    CHECK(value_of(read_be<Price4>(buf.data())) == 123456U);
}

TEST_CASE("Symbol equality and full-width view") {
    std::array<std::uint8_t, 8> buf{'M', 'S', 'F', 'T', 'X', 'Y', 'Z', 'Q'};
    Symbol s = read_symbol(buf.data());
    CHECK(s.view().size() == 8);
    CHECK(s == s);
}
