#pragma once

#include <cstddef>
#include <cstdint>

namespace os::ipc {

inline constexpr std::uint16_t transport_version_v1 = 1;
inline constexpr std::uint16_t wire_header_v1_size = 40;
inline constexpr std::size_t max_inline_payload_size = 64U * 1024U;
inline constexpr std::uint16_t max_handle_count = 16;
inline constexpr std::size_t max_wire_packet_size =
    static_cast<std::size_t>(wire_header_v1_size) + max_inline_payload_size;

namespace errors {
inline constexpr std::uint32_t malformed_message = 1;
inline constexpr std::uint32_t unsupported_version = 2;
inline constexpr std::uint32_t oversized_message = 3;
inline constexpr std::uint32_t buffer_too_small = 4;
inline constexpr std::uint32_t invalid_flags = 5;
inline constexpr std::uint32_t invalid_utf8 = 6;
inline constexpr std::uint32_t unsupported_checksum = 7;
inline constexpr std::uint32_t peer_died = 8;
inline constexpr std::uint32_t transport_failure = 9;
inline constexpr std::uint32_t packet_truncated = 10;
inline constexpr std::uint32_t ancillary_truncated = 11;
inline constexpr std::uint32_t invalid_ancillary = 12;
inline constexpr std::uint32_t missing_credentials = 13;
inline constexpr std::uint32_t handle_count_mismatch = 14;
inline constexpr std::uint32_t invalid_native_handle = 15;
inline constexpr std::uint32_t invalid_channel = 16;
inline constexpr std::uint32_t packet_size_mismatch = 17;
inline constexpr std::uint32_t protocol_violation = 18;
inline constexpr std::uint32_t request_id_exhausted = 19;
}

} // namespace os::ipc
