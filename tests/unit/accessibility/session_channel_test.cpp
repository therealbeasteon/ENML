#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include <sys/wait.h>
#include <unistd.h>

#include <os/accessibility/transport.hpp>
#include <os/ipc/constants.hpp>

namespace {

constexpr os::core::PeerIdentity accessibility_peer{
    .principal = os::accessibility::accessibility_service_principal,
    .user = os::core::UserId{0U},
    .process = os::core::ProcessId{8801U},
};
constexpr os::ui::AccessibilitySessionId session{0x8802U};

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
            .bounds = rect(20U, 120U, 200U, 56U),
            .actions = actions,
            .label = text("Continue"),
        });
    assert(button);

    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto pair = std::move(pair_result).value();

    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        pair[0].close();
        os::ui::AccessibilityBridgeAuthority authority{
            tree,
            os::accessibility::accessibility_service_principal,
            session,
        };
        os::accessibility::AccessibilitySessionServer server{authority, accessibility_peer};
        if (!server.valid()) ::_exit(20);
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
        auto snapshot = server.dispatch_once(pair[1], scratch);
        if (!snapshot) ::_exit(21);
        auto focus = server.dispatch_once(pair[1], scratch);
        if (!focus) ::_exit(22);
        auto focused = tree.focused_node();
        if (!focused || focused.value() != button.value().id) ::_exit(23);
        ::_exit(0);
    }

    pair[1].close();
    os::accessibility::AccessibilitySessionClient client{pair[0]};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    os::ui::AccessibilitySessionSnapshot snapshot{};
    auto snapped = client.snapshot(session, snapshot, scratch);
    assert(snapped);
    assert(snapshot.session == session);
    assert(snapshot.snapshot.semantic.count == 2U);
    assert(snapshot.snapshot.semantic.nodes[1].id == button.value().id);
    assert(snapshot.snapshot.semantic.nodes[1].label.view() == "Continue");

    auto focused = client.dispatch_action(
        {
            .session = session,
            .request = {
                .snapshot_revision = snapshot.snapshot.revision,
                .target = button.value().id,
                .action = os::ui::UiAction::focus,
            },
        },
        scratch);
    assert(focused);
    assert(focused.value().target == button.value().id);
    assert(focused.value().action == os::ui::UiAction::focus);

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    return 0;
}
