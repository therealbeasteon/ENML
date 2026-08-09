#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>

namespace os::storage {

class Directory;

class File final {
public:
    File() noexcept = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&&) noexcept = default;
    File& operator=(File&&) noexcept = default;
    ~File() = default;

    [[nodiscard]] bool valid() const noexcept { return handle_.valid(); }
    [[nodiscard]] bool readable() const noexcept { return readable_; }
    [[nodiscard]] bool writable() const noexcept { return writable_; }

    [[nodiscard]] os::core::Result<std::size_t>
    read_at(std::uint64_t offset, os::core::MutableByteSpan output) noexcept;

    [[nodiscard]] os::core::Result<std::size_t>
    write_at(std::uint64_t offset, os::core::ByteSpan input) noexcept;

    [[nodiscard]] os::core::Result<std::uint64_t> size() const noexcept;
    [[nodiscard]] os::core::Result<void> sync() noexcept;

private:
    friend class Directory;

    File(os::core::NativeHandle handle, bool readable, bool writable) noexcept
        : handle_(static_cast<os::core::NativeHandle&&>(handle)),
          readable_(readable),
          writable_(writable) {}

    os::core::NativeHandle handle_ {};
    bool readable_ {false};
    bool writable_ {false};
};

} // namespace os::storage
