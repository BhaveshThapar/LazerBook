#ifndef LAZERBOOK_BENCH_RAPL_HPP
#define LAZERBOOK_BENCH_RAPL_HPP

#include <cstdint>

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#endif

namespace lazerbook::bench {

// Intel RAPL energy counter reader (Linux/x86 only). Reads the package energy
// register in microjoules; on any other platform available() is false and the
// reads return 0.
class RaplReader {
   public:
    explicit RaplReader(int package = 0) {
#if defined(__linux__)
        std::string const path =
            "/sys/class/powercap/intel-rapl/intel-rapl:" + std::to_string(package) + "/energy_uj";
        fd_ = ::open(path.c_str(), O_RDONLY);
#else
        (void)package;
#endif
    }

    RaplReader(RaplReader const&) = delete;
    RaplReader& operator=(RaplReader const&) = delete;
    RaplReader(RaplReader&&) = delete;
    RaplReader& operator=(RaplReader&&) = delete;

    ~RaplReader() {
#if defined(__linux__)
        if (fd_ >= 0) {
            ::close(fd_);
        }
#endif
    }

    [[nodiscard]] bool available() const noexcept {
#if defined(__linux__)
        return fd_ >= 0;
#else
        return false;
#endif
    }

    [[nodiscard]] std::uint64_t read_uj() const noexcept {
#if defined(__linux__)
        if (fd_ < 0) {
            return 0;
        }
        char buf[32] = {};
        if (::lseek(fd_, 0, SEEK_SET) < 0) {
            return 0;
        }
        ssize_t const n = ::read(fd_, buf, sizeof(buf) - 1);
        if (n <= 0) {
            return 0;
        }
        return std::strtoull(buf, nullptr, 10);
#else
        return 0;
#endif
    }

   private:
#if defined(__linux__)
    int fd_ = -1;
#endif
};

// Brackets a measured region; energy_uj() is the package energy delta.
class EnergyMeasure {
   public:
    explicit EnergyMeasure(int package = 0) : reader_(package) {}

    void start() noexcept { start_uj_ = reader_.read_uj(); }
    void stop() noexcept { stop_uj_ = reader_.read_uj(); }
    [[nodiscard]] bool available() const noexcept { return reader_.available(); }
    [[nodiscard]] std::uint64_t energy_uj() const noexcept {
        return stop_uj_ >= start_uj_ ? stop_uj_ - start_uj_ : 0;
    }

   private:
    RaplReader reader_;
    std::uint64_t start_uj_ = 0;
    std::uint64_t stop_uj_ = 0;
};

}  // namespace lazerbook::bench

#endif  // LAZERBOOK_BENCH_RAPL_HPP
