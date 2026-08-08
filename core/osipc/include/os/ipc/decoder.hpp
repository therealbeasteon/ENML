#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <os/core/result.hpp>
#include <os/core/span.hpp>

namespace os::ipc {

class Decoder final {
public:
    explicit Decoder(os::core::ByteSpan input) noexcept;

    [[nodiscard]] os::core::Result<std::uint8_t> read_u8() noexcept;
    [[nodiscard]] os::core::Result<std::uint16_t> read_u16_le() noexcept;
    [[nodiscard]] os::core::Result<std::uint32_t> read_u32_le() noexcept;
    [[nodiscard]] os::core::Result<std::uint64_t> read_u64_le() noexcept;
    [[nodiscard]] os::core::Result<bool> read_bool() noexcept;

    [[nodiscard]] os::core::Result<os::core::ByteSpan>
    read_raw(std::size_t count) noexcept;

    [[nodiscard]] os::core::Result<os::core::ByteSpan>
    read_bytes(std::size_t maximum) noexcept;

    [[nodiscard]] os::core::Result<std::string_view>
    read_utf8(std::size_t maximum) noexcept;

    [[nodiscard]] os::core::Result<void> require_end() const noexcept;
    [[nodiscard]] std::size_t remaining() const noexcept;

private:
    os::core::ByteSpan input_;
    std::size_t cursor_ {0};
};

[[nodiscard]] bool is_valid_utf8(os::core::ByteSpan bytes) noexcept;

} // namespace os::ipc
