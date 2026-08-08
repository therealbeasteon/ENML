#pragma once

namespace os::core {

class NativeHandle final {
public:
    NativeHandle() noexcept = default;
    explicit NativeHandle(int fd) noexcept : fd_(fd) {}

    NativeHandle(const NativeHandle&) = delete;
    NativeHandle& operator=(const NativeHandle&) = delete;

    NativeHandle(NativeHandle&& other) noexcept;
    NativeHandle& operator=(NativeHandle&& other) noexcept;

    ~NativeHandle();

    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    [[nodiscard]] int native() const noexcept { return fd_; }

    [[nodiscard]] int release_native() noexcept;
    void reset(int new_fd = -1) noexcept;

private:
    int fd_ {-1};
};

} // namespace os::core
