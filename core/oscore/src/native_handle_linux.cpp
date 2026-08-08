#include <os/core/native_handle.hpp>

#include <unistd.h>
#include <utility>

namespace os::core {

NativeHandle::NativeHandle(NativeHandle&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)) {}

NativeHandle& NativeHandle::operator=(NativeHandle&& other) noexcept {
    if (this != &other) {
        reset();
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}

NativeHandle::~NativeHandle() {
    reset();
}

int NativeHandle::release_native() noexcept {
    return std::exchange(fd_, -1);
}

void NativeHandle::reset(int new_fd) noexcept {
    if (fd_ == new_fd) {
        return;
    }
    if (fd_ >= 0) {
        (void)::close(fd_);
    }
    fd_ = new_fd;
}

} // namespace os::core
