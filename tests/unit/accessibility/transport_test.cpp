#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <os/accessibility/transport.hpp>
#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/ui/error.hpp>

namespace {

constexpr os::core::PrincipalId accessibility_principal{
    0x4143434553535452ULL,
    0x414E53504F525401ULL,
};
constexpr os::core::PrincipalId application_principal{
    0x4150505452414E53ULL,
    0x504F525400000001ULL,
};
constexpr os::core::PeerIdentity accessibility_peer{
    .principal = accessibility_principal,
    .user = os::core::UserId{0U},
    .process = os::core::ProcessId{7001U},
};
constexpr os::core::PeerIdentity application_peer{
    .principal = application_principal,
    .user = os::core::UserId{31U},
    .process = os::core::ProcessId{7002U},
};
constexpr os::ui::AccessibilitySessionId session{0x7101U};
constexpr os::ui::AccessibilitySessionId other_session{0x7102U};

[[nodiscard]] os::ui::LogicalRect rect(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t width,
    std::uint32_t height) noexcept {
    return os::ui::LogicalRect{
        .x_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(x)),
        .y_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(y)),
        .width_q6 = os::ui::logical_from_dp(width),
        .height_q6 = os::ui::logical_from_dp(height),
    };
}

[[nodiscard]] os::ui::SemanticText text(std::string_view value) {
    auto result = os::ui::make_semantic_text(value);
    assert(result);
    return result.value();
}

void expect_ui_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::ui);
    assert(error.code == code);
}

void expect_ipc_protocol_error(const os::core::Error& error) {
    assert(error.domain == os::core::ErrorDomain::ipc);
    assert(error.code == os::ipc::errors::protocol_violation);
}

} // namespace

int main() {
    os::ui::SemanticTree tree{rect(0U, 0U, 360U, 800U)};
    assert(tree.valid());

    const auto actions =
        os::ui::action_mask(os::ui::UiAction::activate) |
        os::ui::action_mask(os::ui::UiAction::focus);
    auto button = tree.add(
        tree.root(),
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::button,
            .bounds = rect(24U, 120U, 200U, 56U),
            .actions = actions,
            .label = text("Continue"),
        });
    assert(button);

    os::ui::AccessibilityBridgeAuthority authority{
        tree,
        accessibility_principal,
        session,
    };
    assert(authority.valid());

    std::array<std::byte, os::accessibility::snapshot_request_size_v1> snapshot_request{};
    auto snapshot_request_size = os::accessibility::encode_snapshot_request_v1(
        session,
        snapshot_request);
    assert(snapshot_request_size);
    assert(snapshot_request_size.value() == snapshot_request.size());

    std::array<std::byte, os::accessibility::max_snapshot_response_size_v1> response{};
    auto snapshot_response_size = os::accessibility::dispatch_accessibility_transport_v1(
        authority,
        accessibility_peer,
        os::accessibility::TransportOperation::snapshot,
        {snapshot_request.data(), snapshot_request.size()},
        response);
    assert(snapshot_response_size);
    assert(snapshot_response_size.value() <= response.size());

    os::ui::AccessibilitySessionSnapshot decoded{};
    auto decoded_snapshot = os::accessibility::decode_snapshot_response_v1(
        {response.data(), snapshot_response_size.value()},
        decoded);
    assert(decoded_snapshot);
    assert(decoded.session == session);
    assert(decoded.snapshot.revision == tree.revision());
    assert(decoded.snapshot.semantic.count == 2U);
    assert(decoded.snapshot.semantic.nodes[0].role == os::ui::UiRole::root);
    assert(decoded.snapshot.semantic.nodes[1].id == button.value().id);
    assert(decoded.snapshot.semantic.nodes[1].parent == tree.root());
    assert(decoded.snapshot.semantic.nodes[1].label.view() == "Continue");

    // Unauthorized callers are rejected before a requested session mismatch is
    // considered, avoiding session-id probing through error differences.
    std::array<std::byte, os::accessibility::snapshot_request_size_v1> wrong_snapshot_request{};
    auto wrong_snapshot_size = os::accessibility::encode_snapshot_request_v1(
        other_session,
        wrong_snapshot_request);
    assert(wrong_snapshot_size);
    auto unauthorized = os::accessibility::dispatch_accessibility_transport_v1(
        authority,
        application_peer,
        os::accessibility::TransportOperation::snapshot,
        {wrong_snapshot_request.data(), wrong_snapshot_request.size()},
        response);
    assert(!unauthorized);
    expect_ui_error(unauthorized.error(), os::ui::errors::accessibility_authority_denied);

    auto wrong_session = os::accessibility::dispatch_accessibility_transport_v1(
        authority,
        accessibility_peer,
        os::accessibility::TransportOperation::snapshot,
        {wrong_snapshot_request.data(), wrong_snapshot_request.size()},
        response);
    assert(!wrong_session);
    expect_ui_error(wrong_session.error(), os::ui::errors::accessibility_session_mismatch);

    std::array<std::byte, os::accessibility::action_request_size_v1> action_request{};
    const os::ui::AccessibilitySessionActionRequest focus_request{
        .session = session,
        .request = {
            .snapshot_revision = decoded.snapshot.revision,
            .target = button.value().id,
            .action = os::ui::UiAction::focus,
        },
    };
    auto action_size = os::accessibility::encode_action_request_v1(
        focus_request,
        action_request);
    assert(action_size);

    std::array<std::byte, os::accessibility::action_response_size_v1> action_response{};
    auto focus_response_size = os::accessibility::dispatch_accessibility_transport_v1(
        authority,
        accessibility_peer,
        os::accessibility::TransportOperation::action,
        {action_request.data(), action_request.size()},
        action_response);
    assert(focus_response_size);
    assert(focus_response_size.value() == action_response.size());
    auto focus_event = os::accessibility::decode_action_response_v1(action_response);
    assert(focus_event);
    assert(focus_event.value().target == button.value().id);
    assert(focus_event.value().action == os::ui::UiAction::focus);
    assert(tree.focused_node());
    assert(tree.focused_node().value() == button.value().id);

    // Focus mutated the semantic revision, so replaying the prior action record
    // is stale even though its session/node/action fields are otherwise valid.
    auto stale = os::accessibility::dispatch_accessibility_transport_v1(
        authority,
        accessibility_peer,
        os::accessibility::TransportOperation::action,
        {action_request.data(), action_request.size()},
        action_response);
    assert(!stale);
    expect_ui_error(stale.error(), os::ui::errors::stale_accessibility_snapshot);

    // Snapshot encoding also rejects structurally invalid semantic graphs.
    auto invalid_snapshot = decoded;
    invalid_snapshot.snapshot.semantic.nodes[1].id = invalid_snapshot.snapshot.semantic.nodes[0].id;
    auto invalid_encode = os::accessibility::encode_snapshot_response_v1(
        invalid_snapshot,
        response);
    assert(!invalid_encode);
    expect_ipc_protocol_error(invalid_encode.error());

    // Decoder independently rejects a duplicate node ID received on the wire.
    auto fresh_snapshot = authority.snapshot(accessibility_peer);
    assert(fresh_snapshot);
    auto encoded_fresh = os::accessibility::encode_snapshot_response_v1(
        fresh_snapshot.value(),
        response);
    assert(encoded_fresh);
    assert(fresh_snapshot.value().snapshot.semantic.count == 2U);
    constexpr std::size_t second_node_offset =
        os::accessibility::snapshot_response_header_size_v1 +
        (os::accessibility::max_snapshot_node_wire_size_v1 - os::ui::max_semantic_text_bytes);
    const std::uint32_t root_id = fresh_snapshot.value().snapshot.semantic.nodes[0].id.value();
    response[second_node_offset + 0U] = static_cast<std::byte>(root_id & 0xFFU);
    response[second_node_offset + 1U] = static_cast<std::byte>((root_id >> 8U) & 0xFFU);
    response[second_node_offset + 2U] = static_cast<std::byte>((root_id >> 16U) & 0xFFU);
    response[second_node_offset + 3U] = static_cast<std::byte>((root_id >> 24U) & 0xFFU);
    os::ui::AccessibilitySessionSnapshot malformed{};
    auto malformed_decode = os::accessibility::decode_snapshot_response_v1(
        {response.data(), encoded_fresh.value()},
        malformed);
    assert(!malformed_decode);
    expect_ipc_protocol_error(malformed_decode.error());

    return 0;
}
