#include <lazerbook/framing.hpp>

#include <cerrno>
#include <cstring>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#endif

namespace lazerbook {

MappedFile::~MappedFile() { close(); }

MappedFile::MappedFile(MappedFile&& other) noexcept
    : addr_(std::exchange(other.addr_, nullptr)),
      size_(std::exchange(other.size_, 0)),
      fd_(std::exchange(other.fd_, -1)),
      error_(std::move(other.error_)) {}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        close();
        addr_ = std::exchange(other.addr_, nullptr);
        size_ = std::exchange(other.size_, 0);
        fd_ = std::exchange(other.fd_, -1);
        error_ = std::move(other.error_);
    }
    return *this;
}

#if defined(__unix__) || defined(__APPLE__)

bool MappedFile::open(char const* path) {
    close();
    error_.clear();

    fd_ = ::open(path, O_RDONLY);
    if (fd_ < 0) {
        error_ = std::string("open failed: ") + std::strerror(errno);
        return false;
    }
    struct stat st{};
    if (::fstat(fd_, &st) != 0) {
        error_ = std::string("fstat failed: ") + std::strerror(errno);
        close();
        return false;
    }
    if (st.st_size <= 0) {
        error_ = "file is empty";
        close();
        return false;
    }
    size_ = static_cast<std::size_t>(st.st_size);

    void* p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
    if (p == MAP_FAILED) {
        error_ = std::string("mmap failed: ") + std::strerror(errno);
        addr_ = nullptr;
        close();
        return false;
    }
    addr_ = p;
    return true;
}

void MappedFile::close() noexcept {
    if (addr_ != nullptr) {
        ::munmap(addr_, size_);
        addr_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    size_ = 0;
}

void MappedFile::advise_sequential() const noexcept {
    if (addr_ == nullptr) {
        return;
    }
#if defined(MADV_SEQUENTIAL)
    ::madvise(addr_, size_, MADV_SEQUENTIAL);
#endif
#if defined(MADV_WILLNEED)
    ::madvise(addr_, size_, MADV_WILLNEED);
#endif
}

#else  // no mmap available

bool MappedFile::open(char const* /*path*/) {
    error_ = "memory mapping is not supported on this platform";
    return false;
}
void MappedFile::close() noexcept {}
void MappedFile::advise_sequential() const noexcept {}

#endif

}  // namespace lazerbook
