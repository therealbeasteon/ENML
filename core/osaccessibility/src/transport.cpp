#include <os/accessibility/transport.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>
#include <os/ui/error.hpp>

namespace os::accessibility {
namespace {

inline constexpr std::uint8_t state_visible = 1U << 0U;
inline constexpr std::uint8_t state_enabled = 1U << 1U;
inline constexpr std::uint8_t state_focused = 1U << 2U;
inline constexpr std::uint8_t state_selected = 1U << 3U;
inline constexpr std::uint8_t state_checked = 1U << 4U;
inline constexpr std::uint8_t state_pressed = 1U << 5U;
inline constexpr std::uint8_t known_state_mask =
    state_visible | state_enabled | state_focused |
    state_selected | state_checked | state_pressed;
inline constexpr os::ui::UiActionMask known_action_mask =
    os::ui::action_mask(os::ui::UiAction::activate) |
    os::ui::action_mask(os::ui::UiAction::focus) |
    os::ui::action_mask(os::ui::UiAction::toggle) |
    os::ui::action_mask(os::ui::UiAction::set_text) |
    os::ui::action_mask(os::ui::UiAction::select);

[[nodiscard]] constexpr os::core::Error ipc_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ipc, code);
}

[[nodiscard]] constexpr os::core::Error service_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

[[nodiscard]] constexpr bool role_valid(os::ui::UiRole role) noexcept {
    return role >= os::ui::UiRole::root && role <= os::ui::UiRole::list_item;
}

[[nodiscard]] constexpr bool action_valid(os::ui::UiAction action) noexcept {
    switch (action) {
    case os::ui::UiAction::activate:
    case os::ui::UiAction::focus:
    case os::ui::UiAction::toggle:
    case os::ui::UiAction::set_text:
    case os::ui::UiAction::select:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr std::uint8_t encode_state(const os::ui::UiNodeState& state) noexcept {
    std::uint8_t bits = 0U;
    if (state.visible) bits |= state_visible;
    if (state.enabled) bits |= state_enabled;
    if (state.focused) bits |= state_focused;
    if (state.selected) bits |= state_selected;
    if (state.checked) bits |= state_checked;
    if (state.pressed) bits |= state_pressed;
    return bits;
}

[[nodiscard]] constexpr os::ui::UiNodeState decode_state(std::uint8_t bits) noexcept {
    return os::ui::UiNodeState{
        .visible = (bits & state_visible) != 0U,
        .enabled = (bits & state_enabled) != 0U,
        .focused = (bits & state_focused) != 0U,
        .selected = (bits & state_selected) != 0U,
        .checked = (bits & state_checked) != 0U,
        .pressed = (bits & state_pressed) != 0U,
    };
}

[[nodiscard]] bool snapshot_structure_valid(
    const os::ui::AccessibilitySessionSnapshot& value) noexcept {
    if (value.session.value() == 0U || value.snapshot.revision == 0U ||
        value.snapshot.semantic.count == 0U ||
        value.snapshot.semantic.count > os::ui::max_ui_nodes) {
        return false;
    }

    const std::size_t count = value.snapshot.semantic.count;
    std::array<std::int16_t, os::ui::max_ui_nodes> parent_index{};
    for (std::size_t index = 0U; index < count; ++index) parent_index[index] = -2;

    std::size_t root_count = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
        const auto& node = value.snapshot.semantic.nodes[index];
        if (node.id.value() == 0U || !role_valid(node.role) ||
            !node.bounds.bounded() || !os::ui::semantic_text_valid(node.label) ||
            (node.actions & static_cast<os::ui::UiActionMask>(~known_action_mask)) != 0U) {
            return false;
        }

        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (value.snapshot.semantic.nodes[prior].id == node.id) return false;
        }

        if (node.parent.value() == 0U) {
            if (node.role != os::ui::UiRole::root) return false;
            ++root_count;
            parent_index[index] = -1;
            continue;
        }
        if (node.role == os::ui::UiRole::root || node.parent == node.id) return false;

        bool found = false;
        for (std::size_t candidate = 0U; candidate < count; ++candidate) {
            if (value.snapshot.semantic.nodes[candidate].id == node.parent) {
                if (candidate > static_cast<std::size_t>(std::numeric_limits<std::int16_t>::max())) {
                    return false;
                }
                parent_index[index] = static_cast<std::int16_t>(candidate);
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    if (root_count != 1U) return false;

    // Validate every parent chain is acyclic, reaches the single root and does
    // not exceed the semantic-tree depth limit. Parent indices make this O(N^2)
    // at the fixed 256-node maximum rather than repeatedly searching IDs.
    for (std::size_t start = 0U; start < count; ++start) {
        std::int16_t current = static_cast<std::int16_t>(start);
        std::size_t depth = 0U;
        while (current >= 0) {
            if (depth > static_cast<std::size_t>(os::ui::max_ui_depth)) return false;
            const auto next = parent_index[static_cast<std::size_t>(current)];
            if (next == -2) return false;
            current = next;
            ++depth;
            if (depth > count) return false;
        }
    }
    return true;
}

[[nodiscard]] os::core::Result<void> write_prefix(
    os::ipc::Encoder& encoder,
    std::size_t size) noexcept {
    if (size > std::numeric_limits<std::uint16_t>::max()) {
        return ipc_error(os::ipc::errors::oversized_message);
    }
    auto result = encoder.write_u16_le(transport_version_v1);
    if (!result) return result.error();
    return encoder.write_u16_le(static_cast<std::uint16_t>(size));
}

[[nodiscard]] os::core::Result<void> read_prefix(
    os::ipc::Decoder& decoder,
    std::size_t expected_size) noexcept {
    auto version = decoder.read_u16_le();
    if (!version) return version.error();
    auto size = decoder.read_u16_le();
    if (!size) return size.error();
    if (version.value() != transport_version_v1 ||
        static_cast<std::size_t>(size.value()) != expected_size) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return {};
}

[[nodiscard]] os::core::Result<std::size_t> encoded_snapshot_size(
    const os::ui::AccessibilitySessionSnapshot& snapshot) noexcept {
    if (!snapshot_structure_valid(snapshot)) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    std::size_t size = snapshot_response_header_size_v1;
    for (std::size_t index = 0U; index < snapshot.snapshot.semantic.count; ++index) {
        const auto label_size = snapshot.snapshot.semantic.nodes[index].label.view().size();
        size += max_snapshot_node_wire_size_v1 - os::ui::max_semantic_text_bytes + label_size;
    }
    if (size > max_snapshot_response_size_v1 ||
        size > os::ipc::max_inline_payload_size ||
        size > std::numeric_limits<std::uint16_t>::max()) {
        return ipc_error(os::ipc::errors::oversized_message);
    }
    return size;
}

} // namespace

os::core::Result<std::size_t> encode_snapshot_request_v1(
    os::ui::AccessibilitySessionId session,
    os::core::MutableByteSpan output) noexcept {
    if (session.value() == 0U) return ipc_error(os::ipc::errors::protocol_violation);
    os::ipc::Encoder encoder{output};
    auto result = write_prefix(encoder, snapshot_request_size_v1);
    if (!result) return result.error();
    result = encoder.write_u64_le(session.value());
    if (!result) return result.error();
    return encoder.written().size();
}

os::core::Result<os::ui::AccessibilitySessionId>
decode_snapshot_request_v1(os::core::ByteSpan payload) noexcept {
    if (payload.size() != snapshot_request_size_v1) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    os::ipc::Decoder decoder{payload};
    auto prefix = read_prefix(decoder, snapshot_request_size_v1);
    if (!prefix) return prefix.error();
    auto session = decoder.read_u64_le();
    if (!session) return session.error();
    auto end = decoder.require_end();
    if (!end || session.value() == 0U) return ipc_error(os::ipc::errors::protocol_violation);
    return os::ui::AccessibilitySessionId{session.value()};
}

os::core::Result<std::size_t> encode_action_request_v1(
    const os::ui::AccessibilitySessionActionRequest& request,
    os::core::MutableByteSpan output) noexcept {
    if (request.session.value() == 0U || request.request.snapshot_revision == 0U ||
        request.request.target.value() == 0U || !action_valid(request.request.action)) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    os::ipc::Encoder encoder{output};
    auto result = write_prefix(encoder, action_request_size_v1);
    if (!result) return result.error();
    result = encoder.write_u64_le(request.session.value());
    if (!result) return result.error();
    result = encoder.write_u64_le(request.request.snapshot_revision);
    if (!result) return result.error();
    result = encoder.write_u32_le(request.request.target.value());
    if (!result) return result.error();
    result = encoder.write_u16_le(static_cast<std::uint16_t>(request.request.action));
    if (!result) return result.error();
    return encoder.written().size();
}

os::core::Result<os::ui::AccessibilitySessionActionRequest>
decode_action_request_v1(os::core::ByteSpan payload) noexcept {
    if (payload.size() != action_request_size_v1) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    os::ipc::Decoder decoder{payload};
    auto prefix = read_prefix(decoder, action_request_size_v1);
    if (!prefix) return prefix.error();
    auto session = decoder.read_u64_le();
    if (!session) return session.error();
    auto revision = decoder.read_u64_le();
    if (!revision) return revision.error();
    auto target = decoder.read_u32_le();
    if (!target) return target.error();
    auto action = decoder.read_u16_le();
    if (!action) return action.error();
    auto end = decoder.require_end();
    const auto decoded_action = static_cast<os::ui::UiAction>(action.value());
    if (!end || session.value() == 0U || revision.value() == 0U || target.value() == 0U ||
        !action_valid(decoded_action)) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return os::ui::AccessibilitySessionActionRequest{
        .session = os::ui::AccessibilitySessionId{session.value()},
        .request = {
            .snapshot_revision = revision.value(),
            .target = os::ui::UiNodeId{target.value()},
            .action = decoded_action,
        },
    };
}

os::core::Result<std::size_t> encode_snapshot_response_v1(
    const os::ui::AccessibilitySessionSnapshot& snapshot,
    os::core::MutableByteSpan output) noexcept {
    auto size = encoded_snapshot_size(snapshot);
    if (!size) return size.error();
    if (output.size() < size.value()) return ipc_error(os::ipc::errors::buffer_too_small);

    os::ipc::Encoder encoder{output};
    auto result = write_prefix(encoder, size.value());
    if (!result) return result.error();
    result = encoder.write_u64_le(snapshot.session.value());
    if (!result) return result.error();
    result = encoder.write_u64_le(snapshot.snapshot.revision);
    if (!result) return result.error();
    result = encoder.write_u16_le(static_cast<std::uint16_t>(snapshot.snapshot.semantic.count));
    if (!result) return result.error();

    for (std::size_t index = 0U; index < snapshot.snapshot.semantic.count; ++index) {
        const auto& node = snapshot.snapshot.semantic.nodes[index];
        result = encoder.write_u32_le(node.id.value());
        if (!result) return result.error();
        result = encoder.write_u32_le(node.parent.value());
        if (!result) return result.error();
        result = encoder.write_u8(static_cast<std::uint8_t>(node.role));
        if (!result) return result.error();
        result = encoder.write_u32_le(std::bit_cast<std::uint32_t>(node.bounds.x_q6));
        if (!result) return result.error();
        result = encoder.write_u32_le(std::bit_cast<std::uint32_t>(node.bounds.y_q6));
        if (!result) return result.error();
        result = encoder.write_u32_le(node.bounds.width_q6);
        if (!result) return result.error();
        result = encoder.write_u32_le(node.bounds.height_q6);
        if (!result) return result.error();
        result = encoder.write_u8(encode_state(node.state));
        if (!result) return result.error();
        result = encoder.write_u16_le(node.actions);
        if (!result) return result.error();
        result = encoder.write_utf8(node.label.view(), os::ui::max_semantic_text_bytes);
        if (!result) return result.error();
    }
    if (encoder.written().size() != size.value()) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return encoder.written().size();
}

os::core::Result<void> decode_snapshot_response_v1(
    os::core::ByteSpan payload,
    os::ui::AccessibilitySessionSnapshot& output) noexcept {
    if (payload.size() < snapshot_response_header_size_v1 ||
        payload.size() > max_snapshot_response_size_v1) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }

    output = {};
    os::ipc::Decoder decoder{payload};
    auto version = decoder.read_u16_le();
    if (!version) return version.error();
    auto size = decoder.read_u16_le();
    if (!size) return size.error();
    auto session = decoder.read_u64_le();
    if (!session) return session.error();
    auto revision = decoder.read_u64_le();
    if (!revision) return revision.error();
    auto count = decoder.read_u16_le();
    if (!count) return count.error();

    if (version.value() != transport_version_v1 ||
        static_cast<std::size_t>(size.value()) != payload.size() ||
        session.value() == 0U || revision.value() == 0U || count.value() == 0U ||
        static_cast<std::size_t>(count.value()) > os::ui::max_ui_nodes) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }

    output.session = os::ui::AccessibilitySessionId{session.value()};
    output.snapshot.revision = revision.value();
    output.snapshot.semantic.count = static_cast<std::size_t>(count.value());

    for (std::size_t index = 0U; index < output.snapshot.semantic.count; ++index) {
        auto id = decoder.read_u32_le();
        if (!id) return id.error();
        auto parent = decoder.read_u32_le();
        if (!parent) return parent.error();
        auto role = decoder.read_u8();
        if (!role) return role.error();
        auto x = decoder.read_u32_le();
        if (!x) return x.error();
        auto y = decoder.read_u32_le();
        if (!y) return y.error();
        auto width = decoder.read_u32_le();
        if (!width) return width.error();
        auto height = decoder.read_u32_le();
        if (!height) return height.error();
        auto state = decoder.read_u8();
        if (!state) return state.error();
        auto actions = decoder.read_u16_le();
        if (!actions) return actions.error();
        auto label = decoder.read_utf8(os::ui::max_semantic_text_bytes);
        if (!label) return label.error();

        const auto decoded_role = static_cast<os::ui::UiRole>(role.value());
        if (!role_valid(decoded_role) || (state.value() & static_cast<std::uint8_t>(~known_state_mask)) != 0U ||
            (actions.value() & static_cast<os::ui::UiActionMask>(~known_action_mask)) != 0U) {
            return ipc_error(os::ipc::errors::protocol_violation);
        }
        auto semantic_label = os::ui::make_semantic_text(label.value());
        if (!semantic_label) return ipc_error(os::ipc::errors::protocol_violation);

        output.snapshot.semantic.nodes[index] = os::ui::AccessibilityNode{
            .id = os::ui::UiNodeId{id.value()},
            .parent = os::ui::UiNodeId{parent.value()},
            .role = decoded_role,
            .bounds = {
                .x_q6 = std::bit_cast<std::int32_t>(x.value()),
                .y_q6 = std::bit_cast<std::int32_t>(y.value()),
                .width_q6 = width.value(),
                .height_q6 = height.value(),
            },
            .state = decode_state(state.value()),
            .actions = actions.value(),
            .label = semantic_label.value(),
        };
    }

    auto end = decoder.require_end();
    if (!end || !snapshot_structure_valid(output)) {
        output = {};
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return {};
}

os::core::Result<std::size_t> encode_action_response_v1(
    const os::ui::UiEvent& event,
    os::core::MutableByteSpan output) noexcept {
    if (event.target.value() == 0U || !action_valid(event.action)) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    os::ipc::Encoder encoder{output};
    auto result = write_prefix(encoder, action_response_size_v1);
    if (!result) return result.error();
    result = encoder.write_u32_le(event.target.value());
    if (!result) return result.error();
    result = encoder.write_u16_le(static_cast<std::uint16_t>(event.action));
    if (!result) return result.error();
    return encoder.written().size();
}

os::core::Result<os::ui::UiEvent>
decode_action_response_v1(os::core::ByteSpan payload) noexcept {
    if (payload.size() != action_response_size_v1) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    os::ipc::Decoder decoder{payload};
    auto prefix = read_prefix(decoder, action_response_size_v1);
    if (!prefix) return prefix.error();
    auto target = decoder.read_u32_le();
    if (!target) return target.error();
    auto action = decoder.read_u16_le();
    if (!action) return action.error();
    auto end = decoder.require_end();
    const auto decoded_action = static_cast<os::ui::UiAction>(action.value());
    if (!end || target.value() == 0U || !action_valid(decoded_action)) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return os::ui::UiEvent{
        .target = os::ui::UiNodeId{target.value()},
        .action = decoded_action,
    };
}

os::core::Result<std::size_t> dispatch_accessibility_transport_v1(
    os::ui::AccessibilityBridgeAuthority& authority,
    os::core::PeerIdentity caller,
    TransportOperation operation,
    os::core::ByteSpan request_payload,
    os::core::MutableByteSpan response_output) noexcept {
    if (!authority.valid()) return os::ui::ui_error(os::ui::errors::invalid_tree);

    switch (operation) {
    case TransportOperation::snapshot: {
        auto requested_session = decode_snapshot_request_v1(request_payload);
        if (!requested_session) return requested_session.error();

        // Principal authorization runs before session comparison so an
        // unauthorized caller cannot probe whether a guessed session id exists.
        auto snapshot = authority.snapshot(caller);
        if (!snapshot) return snapshot.error();
        if (snapshot.value().session != requested_session.value()) {
            return os::ui::ui_error(os::ui::errors::accessibility_session_mismatch);
        }
        return encode_snapshot_response_v1(snapshot.value(), response_output);
    }
    case TransportOperation::action: {
        auto request = decode_action_request_v1(request_payload);
        if (!request) return request.error();
        auto event = authority.dispatch(caller, request.value());
        if (!event) return event.error();
        return encode_action_response_v1(event.value(), response_output);
    }
    }
    return service_error(os::core::errors::service::unknown_operation);
}

} // namespace os::accessibility
