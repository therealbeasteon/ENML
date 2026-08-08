#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <os/core/result.hpp>
#include <os/core/span.hpp>

namespace os::ipc {

class Encoder final {
public:
    explicit Encoder(os::core::MutableByteSpan output) noexcept;

    [[nodiscard]] os::core::Result<void> write_u8(std::uint8_t value) noexcept;
    [[nodiscard]] os::core::Result<void> write_u16_le(std::uint16_t value) noexcept;
    [[nodiscard]] os::core::Result<void> write_u32_le(std::uint32_t value) noexcept;
    [[nodiscard]] os::core::Result<void> write_u64_le(std::uint64_t value) noexcept;
    [[nodiscard]] os::core::Result<void> write_bool(bool value) noexcept;

    [[nodiscard]] os::core::Result<void>
    write_raw(os::core::ByteSpan bytes) noexcept;

    [[nodiscard]] os::core::Result<void>
    write_bytes(os::core::ByteSpan bytes, std::size_t maximum) noexcept;

    [[nodiscard]] os::core::Result<void>
    write_utf8(std::string_view value, std::size_t maximum) noexcept;

    [[nodiscard]] os::core::ByteSpan written() const noexcept;
    [[nodiscard]] std::size_t remaining() const noexcept;

private:
    os::core::MutableByteSpan output_;
    std::size_t cursor_ {0};

    [[nodiscard]] os::core::Result<void>
    ensure_capacity(std::size_t count) const noexcept;
};

} // namespace os::ipc
