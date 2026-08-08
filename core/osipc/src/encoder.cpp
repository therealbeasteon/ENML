#include <os/ipc/encoder.hpp>

#include <array>
#include <limits>

#include <os/core/checked.hpp>
#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/decoder.hpp>

namespace os::ipc {
namespace {

[[nodiscard]] constexpr os::core::Error ipc_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ipc, code);
}

} // namespace

Encoder::Encoder(os::core::MutableByteSpan output) noexcept
    : output_(output) {}

os::core::Result<void> Encoder::ensure_capacity(std::size_t count) const noexcept {
    if (count > remaining()) {
        return ipc_error(errors::buffer_too_small);
    }
    return {};
}

os::core::Result<void> Encoder::write_u8(std::uint8_t value) noexcept {
    auto capacity = ensure_capacity(1);
    if (!capacity) {
        return capacity.error();
    }
    output_[cursor_] = static_cast<std::byte>(value);
    ++cursor_;
    return {};
}

os::core::Result<void> Encoder::write_u16_le(std::uint16_t value) noexcept {
    std::array<std::byte, 2> bytes {
        static_cast<std::byte>(value & 0xFFU),
        static_cast<std::byte>((value >> 8U) & 0xFFU),
    };
    return write_raw(bytes);
}

os::core::Result<void> Encoder::write_u32_le(std::uint32_t value) noexcept {
    std::array<std::byte, 4> bytes {};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto shift = static_cast<unsigned int>(i * 8U);
        bytes[i] = static_cast<std::byte>((value >> shift) & 0xFFU);
    }
    return write_raw(bytes);
}

os::core::Result<void> Encoder::write_u64_le(std::uint64_t value) noexcept {
    std::array<std::byte, 8> bytes {};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto shift = static_cast<unsigned int>(i * 8U);
        bytes[i] = static_cast<std::byte>((value >> shift) & 0xFFU);
    }
    return write_raw(bytes);
}

os::core::Result<void> Encoder::write_bool(bool value) noexcept {
    return write_u8(value ? 1U : 0U);
}

os::core::Result<void> Encoder::write_raw(os::core::ByteSpan bytes) noexcept {
    auto capacity = ensure_capacity(bytes.size());
    if (!capacity) {
        return capacity.error();
    }

    for (const auto byte : bytes) {
        output_[cursor_] = byte;
        ++cursor_;
    }
    return {};
}

os::core::Result<void>
Encoder::write_bytes(os::core::ByteSpan bytes, std::size_t maximum) noexcept {
    if (bytes.size() > maximum || bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        return ipc_error(errors::oversized_message);
    }

    auto total_result = os::core::checked_add(sizeof(std::uint32_t), bytes.size());
    if (!total_result) {
        return ipc_error(errors::oversized_message);
    }
    auto capacity = ensure_capacity(total_result.value());
    if (!capacity) {
        return capacity.error();
    }

    auto length_result = write_u32_le(static_cast<std::uint32_t>(bytes.size()));
    if (!length_result) {
        return length_result.error();
    }
    return write_raw(bytes);
}

os::core::Result<void>
Encoder::write_utf8(std::string_view value, std::size_t maximum) noexcept {
    const auto bytes = os::core::ByteSpan{
        reinterpret_cast<const std::byte*>(value.data()), value.size()};
    if (!is_valid_utf8(bytes)) {
        return ipc_error(errors::invalid_utf8);
    }
    return write_bytes(bytes, maximum);
}

os::core::ByteSpan Encoder::written() const noexcept {
    return os::core::ByteSpan{output_.data(), cursor_};
}

std::size_t Encoder::remaining() const noexcept {
    return output_.size() - cursor_;
}

} // namespace os::ipc
