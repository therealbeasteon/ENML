#include <os/ui/design.hpp>
#include <os/ui/tree.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
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

[[nodiscard]] bool contains(
    const std::array<os::ui::UiNodeId, os::ui::max_ui_nodes>& ids,
    std::size_t count,
    os::ui::UiNodeId wanted) {
    for (std::size_t index = 0U; index < count; ++index) {
        if (ids[index] == wanted) return true;
    }
    return false;
}

} // namespace

int main() {
    os::ui::SemanticTree tree{rect(0U, 0U, 360U, 800U)};
    assert(tree.valid());
    assert(tree.revision() == 1U);

    auto initial = tree.take_renderer_delta();
    assert(initial.revision == 1U);
    assert(initial.changed_count == 1U);
    assert(initial.changed[0] == tree.root());
    assert(initial.removed_count == 0U);
    assert(!initial.full_resync_required);

    auto empty = tree.take_renderer_delta();
    assert(empty.changed_count == 0U);
    assert(empty.removed_count == 0U);

    auto content = tree.add(
        tree.root(),
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::container,
            .bounds = rect(0U, 0U, 360U, 800U),
            .style = os::ui::style_tokens::surface,
        });
    assert(content);
    auto button = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::button,
            .bounds = rect(16U, 700U, 328U, 56U),
            .actions = os::ui::action_mask(os::ui::UiAction::activate) |
                os::ui::action_mask(os::ui::UiAction::focus),
            .style = os::ui::style_tokens::primary_action,
            .label = text("Save"),
        });
    assert(button);

    const auto snapshot = tree.renderer_snapshot();
    assert(snapshot.count == 3U);
    assert(snapshot.revision == tree.revision());
    assert(snapshot.nodes[0].id == tree.root());
    assert(snapshot.nodes[1].id == content.value().id);
    assert(snapshot.nodes[2].id == button.value().id);
    assert(snapshot.nodes[2].spec.style == os::ui::style_tokens::primary_action);

    auto created = tree.take_renderer_delta();
    assert(created.changed_count == 2U);
    assert(contains(created.changed, created.changed_count, content.value().id));
    assert(contains(created.changed, created.changed_count, button.value().id));

    const auto before_noop = tree.revision();
    assert(tree.set_style(button.value().id, os::ui::style_tokens::primary_action));
    assert(tree.revision() == before_noop);
    assert(tree.take_renderer_delta().changed_count == 0U);

    auto bad_style = tree.set_style(button.value().id, os::ui::StyleTokenId{65535U});
    assert(!bad_style);
    expect_ui_error(bad_style.error(), os::ui::errors::invalid_style);

    assert(tree.set_label(button.value().id, text("Save changes")));
    assert(tree.set_style(button.value().id, os::ui::style_tokens::secondary_action));
    auto edited = tree.take_renderer_delta();
    assert(edited.changed_count == 1U);
    assert(edited.changed[0] == button.value().id);

    assert(tree.focus(button.value().id));
    auto focused = tree.take_renderer_delta();
    assert(focused.changed_count == 1U);
    assert(focused.changed[0] == button.value().id);

    auto content_state = tree.lookup(content.value().id).value().spec.state;
    content_state.visible = false;
    assert(tree.set_state(content.value().id, content_state));
    auto hidden = tree.take_renderer_delta();
    assert(hidden.changed_count == 2U);
    assert(contains(hidden.changed, hidden.changed_count, content.value().id));
    assert(contains(hidden.changed, hidden.changed_count, button.value().id));
    auto no_focus = tree.focused_node();
    assert(!no_focus);
    expect_ui_error(no_focus.error(), os::ui::errors::no_focus);

    const auto removed_id = button.value().id;
    assert(tree.remove_subtree(content.value().id));
    auto removed = tree.take_renderer_delta();
    assert(removed.removed_count == 2U);
    assert(contains(removed.removed, removed.removed_count, content.value().id));
    assert(contains(removed.removed, removed.removed_count, removed_id));
    assert(tree.renderer_snapshot().count == 1U);

    return 0;
}
