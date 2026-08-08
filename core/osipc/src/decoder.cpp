#include <os/ipc/decoder.hpp>

#include <limits>

#include <os/core/error.hpp>
#include <os/core/checked.hpp>
#include <os/ipc/constants.hpp>

namespace os::ipc {
namespace {

[[nodiscard]] constexpr os::core::Error ipc_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ipc, code);
}

[[nodiscard]] constexpr bool is_continuation(std::uint8_t value) noexcept {
    return (value & 0xC0U) == 0x80U;
}

} // namespace

Decoder::Decoder(os::core::ByteSpan input) noexcept
    : input_(input) {}

os::core::Result<std::uint8_t> Decoder::read_u8() noexcept {
    if (remaining() < 1U) {
        return ipc_error(errors::malformed_message);
    }
    const auto value = std::to_integer<std::uint8_t>(input_[cursor_]);
    ++cursor_;
    return value;
}

os::core::Result<std::uint16_t> Decoder::read_u16_le() noexcept {
    auto bytes_result = read_raw(2);
    if (!bytes_result) {
        return bytes_result.error();
    }
    const auto bytes = bytes_result.value();
    const auto b0 = static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[0]));
    const auto b1 = static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[1]));
    return static_cast<std::uint16_t>(b0 | static_cast<std::uint16_t>(b1 << 8U));
}

os::core::Result<std::uint32_t> Decoder::read_u32_le() noexcept {
    auto bytes_result = read_raw(4);
    if (!bytes_result) {
        return bytes_result.error();
    }
    const auto bytes = bytes_result.value();
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto shift = static_cast<unsigned int>(i * 8U);
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[i])) << shift;
    }
    return value;
}

os::core::Result<std::uint64_t> Decoder::read_u64_le() noexcept {
    auto bytes_result = read_raw(8);
    if (!bytes_result) {
        return bytes_result.error();
    }
    const auto bytes = bytes_result.value();
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto shift = static_cast<unsigned int>(i * 8U);
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[i])) << shift;
    }
    return value;
}

os::core::Result<bool> Decoder::read_bool() noexcept {
    auto value_result = read_u8();
    if (!value_result) {
        return value_result.error();
    }
    const auto value = value_result.value();
    if (value == 0U) {
        return false;
    }
    if (value == 1U) {
        return true;
    }
    return ipc_error(errors::malformed_message);
}

os::core::Result<os::core::ByteSpan>
Decoder::read_raw(std::size_t count) noexcept {
    if (!os::core::range_fits(input_.size(), cursor_, count)) {
        return ipc_error(errors::malformed_message);
    }
    const auto result = input_.subspan(cursor_, count);
    cursor_ += count;
    return result;
}

os::core::Result<os::core::ByteSpan>
Decoder::read_bytes(std::size_t maximum) noexcept {
    auto length_result = read_u32_le();
    if (!length_result) {
        return length_result.error();
    }
    const auto length = static_cast<std::size_t>(length_result.value());
    if (length > maximum) {
        return ipc_error(errors::oversized_message);
    }
    return read_raw(length);
}

os::core::Result<std::string_view>
Decoder::read_utf8(std::size_t maximum) noexcept {
    auto bytes_result = read_bytes(maximum);
    if (!bytes_result) {
        return bytes_result.error();
    }
    const auto bytes = bytes_result.value();
    if (!is_valid_utf8(bytes)) {
        return ipc_error(errors::invalid_utf8);
    }
    return std::string_view{
        reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

os::core::Result<void> Decoder::require_end() const noexcept {
    if (remaining() != 0U) {
        return ipc_error(errors::malformed_message);
    }
    return {};
}

std::size_t Decoder::remaining() const noexcept {
    return input_.size() - cursor_;
}

bool is_valid_utf8(os::core::ByteSpan bytes) noexcept {
    std::size_t i = 0;
    while (i < bytes.size()) {
        const auto b0 = std::to_integer<std::uint8_t>(bytes[i]);
        if (b0 <= 0x7FU) {
            ++i;
            continue;
        }

        if (b0 >= 0xC2U && b0 <= 0xDFU) {
            if (i + 1U >= bytes.size()) {
                return false;
            }
            const auto b1 = std::to_integer<std::uint8_t>(bytes[i + 1U]);
            if (!is_continuation(b1)) {
                return false;
            }
            i += 2U;
            continue;
        }

        if (b0 >= 0xE0U && b0 <= 0xEFU) {
            if (i + 2U >= bytes.size()) {
                return false;
            }
            const auto b1 = std::to_integer<std::uint8_t>(bytes[i + 1U]);
            const auto b2 = std::to_integer<std::uint8_t>(bytes[i + 2U]);
            if (!is_continuation(b1) || !is_continuation(b2)) {
                return false;
            }
            if (b0 == 0xE0U && b1 < 0xA0U) {
                return false; // overlong
            }
            if (b0 == 0xEDU && b1 >= 0xA0U) {
                return false; // surrogate range
            }
            i += 3U;
            continue;
        }

        if (b0 >= 0xF0U && b0 <= 0xF4U) {
            if (i + 3U >= bytes.size()) {
                return false;
            }
            const auto b1 = std::to_integer<std::uint8_t>(bytes[i + 1U]);
            const auto b2 = std::to_integer<std::uint8_t>(bytes[i + 2U]);
            const auto b3 = std::to_integer<std::uint8_t>(bytes[i + 3U]);
            if (!is_continuation(b1) || !is_continuation(b2) || !is_continuation(b3)) {
                return false;
            }
            if (b0 == 0xF0U && b1 < 0x90U) {
                return false; // overlong
            }
            if (b0 == 0xF4U && b1 > 0x8FU) {
                return false; // > U+10FFFF
            }
            i += 4U;
            continue;
        }

        return false;
    }
    return true;
}

} // namespace os::ipc
