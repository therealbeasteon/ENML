#include <os/ui/tree.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <string_view>

#include <os/core/error.hpp>
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
    os::ui::SemanticTree invalid{os::ui::LogicalRect{}};
    assert(!invalid.valid());

    os::ui::SemanticTree tree{rect(0U, 0U, 360U, 800U)};
    assert(tree.valid());
    assert(tree.root().value() == 1U);
    assert(tree.node_count() == 1U);

    auto content = tree.add(
        tree.root(),
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::container,
            .bounds = rect(0U, 0U, 360U, 800U),
        });
    assert(content);

    const auto interactive_actions =
        os::ui::action_mask(os::ui::UiAction::activate) |
        os::ui::action_mask(os::ui::UiAction::focus);
    auto button = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::button,
            .bounds = rect(16U, 720U, 328U, 56U),
            .actions = interactive_actions,
            .label = text("Save"),
        });
    assert(button);

    auto button_text = tree.add(
        button.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::text,
            .bounds = rect(140U, 736U, 80U, 24U),
            .label = text("Save"),
        });
    assert(button_text);

    auto unlabeled_button = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::button,
            .bounds = rect(16U, 640U, 328U, 56U),
            .actions = interactive_actions,
        });
    assert(!unlabeled_button);
    expect_ui_error(unlabeled_button.error(), os::ui::errors::invalid_text);

    auto invalid_checked = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::button,
            .bounds = rect(16U, 640U, 328U, 56U),
            .actions = interactive_actions,
            .state = os::ui::UiNodeState{.checked = true},
            .label = text("Wrong state"),
        });
    assert(!invalid_checked);
    expect_ui_error(invalid_checked.error(), os::ui::errors::invalid_state);

    auto toggle = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::toggle,
            .bounds = rect(16U, 640U, 328U, 56U),
            .actions = os::ui::action_mask(os::ui::UiAction::activate) |
                os::ui::action_mask(os::ui::UiAction::focus) |
                os::ui::action_mask(os::ui::UiAction::toggle),
            .state = os::ui::UiNodeState{.checked = true},
            .label = text("Airplane mode"),
        });
    assert(toggle);

    auto focus_button = tree.focus(button.value().id);
    assert(focus_button);
    assert(tree.focused_node());
    assert(tree.focused_node().value() == button.value().id);

    auto focus_toggle = tree.focus(toggle.value().id);
    assert(focus_toggle);
    assert(tree.focused_node().value() == toggle.value().id);
    auto old_button = tree.lookup(button.value().id);
    assert(old_button);
    assert(!old_button.value().spec.state.focused);

    auto activate = tree.dispatch_action(button.value().id, os::ui::UiAction::activate);
    assert(activate);
    assert(activate.value().target == button.value().id);

    auto bad_select = tree.dispatch_action(toggle.value().id, os::ui::UiAction::select);
    assert(!bad_select);
    expect_ui_error(bad_select.error(), os::ui::errors::invalid_action);

    auto button_state = tree.lookup(button.value().id).value().spec.state;
    button_state.enabled = false;
    button_state.focused = false;
    auto disable = tree.set_state(button.value().id, button_state);
    assert(disable);
    auto disabled_activate = tree.dispatch_action(button.value().id, os::ui::UiAction::activate);
    assert(!disabled_activate);
    expect_ui_error(disabled_activate.error(), os::ui::errors::invalid_state);

    auto decorative_group = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::container,
            .bounds = rect(0U, 80U, 360U, 80U),
            .accessibility_hidden = true,
        });
    assert(decorative_group);
    auto accessible_text = tree.add(
        decorative_group.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::text,
            .bounds = rect(16U, 96U, 200U, 24U),
            .label = text("Visible meaning"),
        });
    assert(accessible_text);

    auto hidden_group = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::container,
            .bounds = rect(0U, 180U, 360U, 80U),
            .state = os::ui::UiNodeState{.visible = false},
        });
    assert(hidden_group);
    auto hidden_text = tree.add(
        hidden_group.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::text,
            .bounds = rect(16U, 196U, 200U, 24U),
            .label = text("Should not project"),
        });
    assert(hidden_text);

    const auto accessibility = tree.accessibility_snapshot();
    bool saw_accessible_text = false;
    bool saw_decorative_group = false;
    bool saw_hidden_text = false;
    for (std::size_t index = 0U; index < accessibility.count; ++index) {
        const auto& node = accessibility.nodes[index];
        if (node.id == accessible_text.value().id) {
            saw_accessible_text = true;
            assert(node.parent == content.value().id);
            assert(node.label.view() == "Visible meaning");
        }
        if (node.id == decorative_group.value().id) saw_decorative_group = true;
        if (node.id == hidden_text.value().id) saw_hidden_text = true;
    }
    assert(saw_accessible_text);
    assert(!saw_decorative_group);
    assert(!saw_hidden_text);

    auto removable = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::container,
            .bounds = rect(0U, 300U, 360U, 100U),
        });
    assert(removable);
    auto removable_child = tree.add(
        removable.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::text,
            .bounds = rect(16U, 316U, 100U, 24U),
            .label = text("Old"),
        });
    assert(removable_child);
    const std::uint32_t old_child_id = removable_child.value().id.value();
    assert(tree.remove_subtree(removable.value().id));
    auto stale = tree.lookup(removable_child.value().id);
    assert(!stale);
    expect_ui_error(stale.error(), os::ui::errors::invalid_node);

    auto replacement = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::container,
            .bounds = rect(0U, 300U, 360U, 100U),
        });
    assert(replacement);
    assert(replacement.value().id.value() > old_child_id);

    auto depth_parent = replacement.value().id;
    for (std::uint8_t depth = replacement.value().depth; depth < os::ui::max_ui_depth; ++depth) {
        auto next = tree.add(
            depth_parent,
            os::ui::UiNodeSpec{
                .role = os::ui::UiRole::container,
                .bounds = rect(1U, 1U, 10U, 10U),
            });
        assert(next);
        depth_parent = next.value().id;
    }
    auto too_deep = tree.add(
        depth_parent,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::container,
            .bounds = rect(1U, 1U, 10U, 10U),
        });
    assert(!too_deep);
    expect_ui_error(too_deep.error(), os::ui::errors::depth_limit);

    auto child_limit_parent = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::container,
            .bounds = rect(0U, 420U, 360U, 120U),
        });
    assert(child_limit_parent);
    for (std::size_t index = 0U; index < os::ui::max_ui_children_per_node; ++index) {
        auto child = tree.add(
            child_limit_parent.value().id,
            os::ui::UiNodeSpec{
                .role = os::ui::UiRole::text,
                .bounds = rect(1U, 1U, 10U, 10U),
                .label = text("x"),
            });
        assert(child);
    }
    auto too_many_children = tree.add(
        child_limit_parent.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::text,
            .bounds = rect(1U, 1U, 10U, 10U),
            .label = text("x"),
        });
    assert(!too_many_children);
    expect_ui_error(too_many_children.error(), os::ui::errors::child_limit);

    const char malformed_bytes[] = {static_cast<char>(0xC0), static_cast<char>(0x80)};
    auto malformed = os::ui::make_semantic_text(
        std::string_view{malformed_bytes, sizeof(malformed_bytes)});
    assert(!malformed);
    expect_ui_error(malformed.error(), os::ui::errors::invalid_text);

    std::array<char, os::ui::max_semantic_text_bytes + 1U> oversized{};
    oversized.fill('a');
    auto too_long = os::ui::make_semantic_text(
        std::string_view{oversized.data(), oversized.size()});
    assert(!too_long);
    expect_ui_error(too_long.error(), os::ui::errors::text_too_long);

    os::ui::SemanticText forged{};
    forged.length = static_cast<std::uint16_t>(os::ui::max_semantic_text_bytes + 1U);
    auto forged_label = tree.set_label(accessible_text.value().id, forged);
    assert(!forged_label);
    expect_ui_error(forged_label.error(), os::ui::errors::invalid_text);

    auto remove_root = tree.remove_subtree(tree.root());
    assert(!remove_root);
    expect_ui_error(remove_root.error(), os::ui::errors::root_immutable);

    return 0;
}
