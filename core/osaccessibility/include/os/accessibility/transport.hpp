#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/ui/accessibility.hpp>

namespace os::accessibility {

// This library sits above osui + osipc so the semantic UI core remains free of
// transport dependencies. It defines bounded wire records and an authorization
// dispatcher, but deliberately does not choose how a platform service brokers
// an application/session endpoint.
enum class TransportOperation : std::uint32_t {
    snapshot = 1U,
    action = 2U,
};

inline constexpr std::uint16_t transport_version_v1 = 1U;
inline constexpr std::size_t snapshot_request_size_v1 = 12U;
inline constexpr std::size_t action_request_size_v1 = 26U;
inline constexpr std::size_t action_response_size_v1 = 10U;

// Per-node maximum:
// id(4) + parent(4) + role(1) + bounds(16) + state(1) + actions(2) +
// UTF-8 length(4) + label(max 160) = 192 bytes.
inline constexpr std::size_t max_snapshot_node_wire_size_v1 =
    4U + 4U + 1U + 16U + 1U + 2U + 4U + os::ui::max_semantic_text_bytes;
inline constexpr std::size_t snapshot_response_header_size_v1 = 22U;
inline constexpr std::size_t max_snapshot_response_size_v1 =
    snapshot_response_header_size_v1 +
    os::ui::max_ui_nodes * max_snapshot_node_wire_size_v1;

static_assert(max_snapshot_response_size_v1 < 64U * 1024U);

[[nodiscard]] os::core::Result<std::size_t> encode_snapshot_request_v1(
    os::ui::AccessibilitySessionId session,
    os::core::MutableByteSpan output) noexcept;

[[nodiscard]] os::core::Result<os::ui::AccessibilitySessionId>
decode_snapshot_request_v1(os::core::ByteSpan payload) noexcept;

[[nodiscard]] os::core::Result<std::size_t> encode_action_request_v1(
    const os::ui::AccessibilitySessionActionRequest& request,
    os::core::MutableByteSpan output) noexcept;

[[nodiscard]] os::core::Result<os::ui::AccessibilitySessionActionRequest>
decode_action_request_v1(os::core::ByteSpan payload) noexcept;

[[nodiscard]] os::core::Result<std::size_t> encode_snapshot_response_v1(
    const os::ui::AccessibilitySessionSnapshot& snapshot,
    os::core::MutableByteSpan output) noexcept;

// `output` is meaningful only on success. Decoding validates fixed bounds,
// UTF-8, known roles/actions/state bits, unique IDs, one root, valid parents and
// absence of parent cycles before returning.
[[nodiscard]] os::core::Result<void> decode_snapshot_response_v1(
    os::core::ByteSpan payload,
    os::ui::AccessibilitySessionSnapshot& output) noexcept;

[[nodiscard]] os::core::Result<std::size_t> encode_action_response_v1(
    const os::ui::UiEvent& event,
    os::core::MutableByteSpan output) noexcept;

[[nodiscard]] os::core::Result<os::ui::UiEvent>
decode_action_response_v1(os::core::ByteSpan payload) noexcept;

// Transport-neutral authenticated dispatch seam. An outer service/capability
// layer supplies the already-resolved runtime PeerIdentity; this function then
// applies exact AccessibilitySessionId + AccessibilityBridgeAuthority checks and
// encodes only bounded records. It creates no worker, queue or polling loop.
[[nodiscard]] os::core::Result<std::size_t> dispatch_accessibility_transport_v1(
    os::ui::AccessibilityBridgeAuthority& authority,
    os::core::PeerIdentity caller,
    TransportOperation operation,
    os::core::ByteSpan request_payload,
    os::core::MutableByteSpan response_output) noexcept;

} // namespace os::accessibility
