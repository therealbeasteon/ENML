#include <cassert>
#include <cstdint>
#include <string_view>

#include <os/core/error.hpp>
#include <os/ui/accessibility.hpp>
#include <os/ui/error.hpp>

namespace {

[[nodiscard]] os::ui::LogicalRect rect(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t width,
    std::uint32_t height) {
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

} // namespace

int main() {
    os::ui::SemanticTree tree{rect(0U, 0U, 360U, 800U)};
    assert(tree.valid());

    auto content = tree.add(
        tree.root(),
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::container,
            .bounds = rect(0U, 0U, 360U, 800U),
        });
    assert(content);

    const auto button_actions =
        os::ui::action_mask(os::ui::UiAction::activate) |
        os::ui::action_mask(os::ui::UiAction::focus);
    auto button = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::button,
            .bounds = rect(20U, 200U, 180U, 56U),
            .actions = button_actions,
            .label = text("Continue"),
        });
    assert(button);

    const auto toggle_actions =
        os::ui::action_mask(os::ui::UiAction::activate) |
        os::ui::action_mask(os::ui::UiAction::focus) |
        os::ui::action_mask(os::ui::UiAction::toggle);
    auto toggle = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::toggle,
            .bounds = rect(20U, 280U, 180U, 56U),
            .actions = toggle_actions,
            .label = text("Airplane mode"),
        });
    assert(toggle);

    auto snapshot = os::ui::accessibility_service_snapshot(tree);
    assert(snapshot);
    assert(snapshot.value().revision == tree.revision());
    assert(snapshot.value().semantic.count >= 4U);

    bool saw_button = false;
    bool saw_toggle = false;
    for (std::size_t index = 0U; index < snapshot.value().semantic.count; ++index) {
        const auto& node = snapshot.value().semantic.nodes[index];
        if (node.id == button.value().id) saw_button = true;
        if (node.id == toggle.value().id) saw_toggle = true;
    }
    assert(saw_button && saw_toggle);

    auto focus = os::ui::dispatch_accessibility_action(
        tree,
        {
            .snapshot_revision = snapshot.value().revision,
            .target = button.value().id,
            .action = os::ui::UiAction::focus,
        });
    assert(focus);
    assert(focus.value().target == button.value().id);
    assert(tree.focused_node());
    assert(tree.focused_node().value() == button.value().id);

    // Focus changed semantic state/revision. Replaying an action from the old
    // accessibility snapshot must not target the now-newer tree implicitly.
    auto stale = os::ui::dispatch_accessibility_action(
        tree,
        {
            .snapshot_revision = snapshot.value().revision,
            .target = toggle.value().id,
            .action = os::ui::UiAction::toggle,
        });
    assert(!stale);
    expect_ui_error(stale.error(), os::ui::errors::stale_accessibility_snapshot);

    auto fresh = os::ui::accessibility_service_snapshot(tree);
    assert(fresh);
    auto toggle_event = os::ui::dispatch_accessibility_action(
        tree,
        {
            .snapshot_revision = fresh.value().revision,
            .target = toggle.value().id,
            .action = os::ui::UiAction::toggle,
        });
    assert(toggle_event);
    assert(toggle_event.value().target == toggle.value().id);
    assert(toggle_event.value().action == os::ui::UiAction::toggle);

    auto no_revision = os::ui::dispatch_accessibility_action(
        tree,
        {
            .snapshot_revision = 0U,
            .target = button.value().id,
            .action = os::ui::UiAction::activate,
        });
    assert(!no_revision);
    expect_ui_error(no_revision.error(), os::ui::errors::invalid_accessibility_snapshot);

    auto current = os::ui::accessibility_service_snapshot(tree);
    assert(current);
    auto unsupported_text = os::ui::dispatch_accessibility_action(
        tree,
        {
            .snapshot_revision = current.value().revision,
            .target = button.value().id,
            .action = os::ui::UiAction::set_text,
        });
    assert(!unsupported_text);
    expect_ui_error(
        unsupported_text.error(),
        os::ui::errors::invalid_accessibility_action);

    assert(tree.remove_subtree(button.value().id));
    auto after_remove = os::ui::accessibility_service_snapshot(tree);
    assert(after_remove);
    auto removed_target = os::ui::dispatch_accessibility_action(
        tree,
        {
            .snapshot_revision = after_remove.value().revision,
            .target = button.value().id,
            .action = os::ui::UiAction::activate,
        });
    assert(!removed_target);
    expect_ui_error(removed_target.error(), os::ui::errors::invalid_node);

    os::ui::SemanticTree invalid{os::ui::LogicalRect{}};
    auto invalid_snapshot = os::ui::accessibility_service_snapshot(invalid);
    assert(!invalid_snapshot);
    expect_ui_error(invalid_snapshot.error(), os::ui::errors::invalid_tree);

    return 0;
}
