#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/core/strong_id.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>

namespace os::ipc {

inline constexpr std::array<std::byte, 4> wire_magic {
    std::byte{'O'}, std::byte{'S'}, std::byte{'I'}, std::byte{'P'}
};

enum class WireFlag : std::uint32_t {
    request     = 1U << 0U,
    response    = 1U << 1U,
    event       = 1U << 2U,
    error       = 1U << 3U,
    oneway      = 1U << 4U,
    cancellable = 1U << 5U,
    has_handles = 1U << 6U,
};

[[nodiscard]] constexpr std::uint32_t flag_value(WireFlag flag) noexcept {
    return static_cast<std::uint32_t>(flag);
}

[[nodiscard]] constexpr std::uint32_t operator|(WireFlag lhs, WireFlag rhs) noexcept {
    return flag_value(lhs) | flag_value(rhs);
}

struct WireHeaderV1 final {
    std::uint32_t flags {0};
    os::core::ServiceId service_id {};
    std::uint32_t operation_id {0};
    os::core::RequestId request_id {};
    std::uint32_t payload_size {0};
    std::uint16_t handle_count {0};
    std::uint32_t payload_checksum {0};

    [[nodiscard]] friend constexpr bool
    operator==(const WireHeaderV1&, const WireHeaderV1&) = default;
};

[[nodiscard]] os::core::Result<void>
validate_wire_flags(std::uint32_t flags, std::uint16_t handle_count) noexcept;

[[nodiscard]] os::core::Result<void>
encode_wire_header_v1(Encoder& encoder, const WireHeaderV1& header) noexcept;

[[nodiscard]] os::core::Result<WireHeaderV1>
decode_wire_header_v1(Decoder& decoder) noexcept;

} // namespace os::ipc
