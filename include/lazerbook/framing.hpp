#ifndef LAZERBOOK_FRAMING_HPP
#define LAZERBOOK_FRAMING_HPP

#include <lazerbook/itch.hpp>
#include <lazerbook/types.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

// Framing for NASDAQ ITCH 5.0 streams.
//
// itch::parse and itch::visit both operate on a buffer whose first byte is the
// message-type byte -- they have no idea where a message ends in a stream. The
// distributed day files are BinaryFILE: a 2-byte big-endian length followed by
// that many message bytes, repeated. Without this layer nothing in the project
// could read a real capture; replay_validate got its lengths from the
// generator that produced the bytes, which tests the decoder but never the
// framing.
namespace lazerbook {

// Walks [2-byte BE length][payload] records over a byte range.
//
// Non-owning: the caller keeps the mapping alive. next() returns the payload
// span for one message and advances, or an empty span at end of stream / on a
// malformed record. Malformed records are counted rather than thrown, because
// a truncated tail is normal on a partial capture.
class BinaryFileReader {
   public:
    struct Stats {
        std::uint64_t messages = 0;
        std::uint64_t bytes = 0;
        std::uint64_t truncated = 0;        // length prefix ran past the buffer
        std::uint64_t zero_length = 0;      // 0-length record
        std::uint64_t length_mismatch = 0;  // framed length != spec length
    };

    explicit BinaryFileReader(std::span<std::uint8_t const> data) noexcept : data_(data) {}

    // Payload of the next message, or empty when the stream is exhausted.
    [[nodiscard]] std::span<std::uint8_t const> next() noexcept {
        if (pos_ + 2 > data_.size()) {
            if (pos_ != data_.size()) {
                ++stats_.truncated;
                pos_ = data_.size();
            }
            return {};
        }
        std::size_t const len = read_be<std::uint16_t>(data_.data() + pos_);
        if (len == 0) {
            ++stats_.zero_length;
            pos_ = data_.size();  // cannot make progress; stop rather than spin
            return {};
        }
        if (pos_ + 2 + len > data_.size()) {
            ++stats_.truncated;
            pos_ = data_.size();
            return {};
        }
        std::uint8_t const* payload = data_.data() + pos_ + 2;
        pos_ += 2 + len;

        // The framed length should agree with the spec length for the type.
        // Disagreement means the stream is desynchronised or the capture is
        // from a different version; count it and hand the bytes over anyway so
        // the caller can decide.
        auto const type = static_cast<itch::MessageType>(static_cast<char>(payload[0]));
        std::size_t const want = itch::expected_length(type);
        if (want != 0 && want != len) {
            ++stats_.length_mismatch;
        }

        ++stats_.messages;
        stats_.bytes += len;
        return {payload, len};
    }

    [[nodiscard]] bool done() const noexcept { return pos_ >= data_.size(); }
    [[nodiscard]] std::size_t position() const noexcept { return pos_; }
    [[nodiscard]] Stats const& stats() const noexcept { return stats_; }

   private:
    std::span<std::uint8_t const> data_;
    std::size_t pos_ = 0;
    Stats stats_;
};

// Read-only memory mapping of a file. Used instead of read() because a full
// ITCH day is ~11GB: mapping it lets the kernel stream pages in and keeps the
// bytes out of a userspace copy, which matters when the per-message budget is
// tens of nanoseconds.
class MappedFile {
   public:
    MappedFile() = default;
    ~MappedFile();

    MappedFile(MappedFile const&) = delete;
    MappedFile& operator=(MappedFile const&) = delete;
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    // Returns false and sets error() on failure.
    [[nodiscard]] bool open(char const* path);
    void close() noexcept;

    [[nodiscard]] std::span<std::uint8_t const> bytes() const noexcept {
        return {static_cast<std::uint8_t const*>(addr_), size_};
    }
    [[nodiscard]] bool is_open() const noexcept { return addr_ != nullptr; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::string const& error() const noexcept { return error_; }

    // Hint that access is sequential and the pages are wanted soon. Best
    // effort; ignored where unsupported.
    void advise_sequential() const noexcept;

   private:
    void* addr_ = nullptr;
    std::size_t size_ = 0;
    int fd_ = -1;
    std::string error_;
};

}  // namespace lazerbook

#endif  // LAZERBOOK_FRAMING_HPP
