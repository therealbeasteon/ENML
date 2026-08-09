#include <os/ui/input.hpp>

#include <cassert>
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

[[nodiscard]] os::ui::LogicalPoint point(std::uint32_t x, std::uint32_t y) {
    return os::ui::LogicalPoint{
        .x_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(x)),
        .y_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(y)),
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

    // A text child is visually/semantically deeper than its button parent but
    // has no pointer action. Routing must climb the same hit path to the button
    // instead of requiring applications to duplicate hit regions on labels.
    auto primary = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::button,
            .bounds = rect(20U, 200U, 160U, 64U),
            .actions = button_actions,
            .label = text("Continue"),
        });
    assert(primary);
    auto primary_text = tree.add(
        primary.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::text,
            .bounds = rect(48U, 220U, 100U, 24U),
            .label = text("Continue"),
        });
    assert(primary_text);

    auto nested_hit = os::ui::route_pointer_action(
        tree.renderer_snapshot(),
        point(60U, 230U),
        os::ui::UiAction::activate);
    assert(nested_hit);
    assert(nested_hit.value().target == primary.value().id);
    assert(nested_hit.value().role == os::ui::UiRole::button);

    auto focus = os::ui::dispatch_pointer_action(
        tree,
        point(60U, 230U),
        os::ui::UiAction::focus);
    assert(focus);
    assert(focus.value().target == primary.value().id);
    assert(tree.focused_node());
    assert(tree.focused_node().value() == primary.value().id);

    // Equal-depth overlapping siblings follow current paint order: larger
    // monotonic UiNodeId is painted later and therefore owns the topmost hit.
    auto lower = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::button,
            .bounds = rect(20U, 320U, 180U, 72U),
            .actions = button_actions,
            .label = text("Lower"),
        });
    assert(lower);
    auto upper = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::button,
            .bounds = rect(60U, 340U, 180U, 72U),
            .actions = button_actions,
            .label = text("Upper"),
        });
    assert(upper);
    assert(upper.value().id.value() > lower.value().id.value());

    auto overlap = os::ui::route_pointer_action(
        tree.renderer_snapshot(),
        point(100U, 360U),
        os::ui::UiAction::activate);
    assert(overlap);
    assert(overlap.value().target == upper.value().id);

    // A visible topmost semantic overlay intentionally blocks click-through.
    // This is the safer default until a future explicit pointer-transparency
    // semantic exists; decoration cannot accidentally expose an unrelated
    // lower sibling as the input target.
    auto blocking_overlay = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::container,
            .bounds = rect(60U, 340U, 180U, 72U),
        });
    assert(blocking_overlay);
    auto blocked = os::ui::route_pointer_action(
        tree.renderer_snapshot(),
        point(100U, 360U),
        os::ui::UiAction::activate);
    assert(!blocked);
    expect_ui_error(blocked.error(), os::ui::errors::input_no_target);

    // Hidden overlays are absent from the effective hit stack even when they
    // were inserted later than the visible control beneath them.
    auto visible_button = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::button,
            .bounds = rect(20U, 440U, 180U, 72U),
            .actions = button_actions,
            .label = text("Visible"),
        });
    assert(visible_button);
    auto hidden_overlay = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::container,
            .bounds = rect(20U, 440U, 180U, 72U),
            .state = os::ui::UiNodeState{.visible = false},
        });
    assert(hidden_overlay);
    assert(hidden_overlay.value().id.value() > visible_button.value().id.value());

    auto visible_hit = os::ui::route_pointer_action(
        tree.renderer_snapshot(),
        point(80U, 470U),
        os::ui::UiAction::activate);
    assert(visible_hit);
    assert(visible_hit.value().target == visible_button.value().id);

    // A disabled topmost control blocks click-through into an overlapping lower
    // control. Input state must not silently bypass what the user sees on top.
    auto lower_again = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::button,
            .bounds = rect(20U, 560U, 180U, 72U),
            .actions = button_actions,
            .label = text("Underlying"),
        });
    assert(lower_again);
    auto disabled_top = tree.add(
        content.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::button,
            .bounds = rect(60U, 580U, 180U, 72U),
            .actions = button_actions,
            .state = os::ui::UiNodeState{.enabled = false},
            .label = text("Disabled"),
        });
    assert(disabled_top);

    auto disabled_block = os::ui::route_pointer_action(
        tree.renderer_snapshot(),
        point(100U, 600U),
        os::ui::UiAction::activate);
    assert(!disabled_block);
    expect_ui_error(disabled_block.error(), os::ui::errors::input_no_target);

    auto invalid_pointer_action = os::ui::route_pointer_action(
        tree.renderer_snapshot(),
        point(60U, 230U),
        os::ui::UiAction::set_text);
    assert(!invalid_pointer_action);
    expect_ui_error(
        invalid_pointer_action.error(),
        os::ui::errors::invalid_input_action);

    const auto too_far = static_cast<std::int32_t>(os::ui::max_logical_dimension_q6 + 1U);
    auto invalid_point = os::ui::route_pointer_action(
        tree.renderer_snapshot(),
        os::ui::LogicalPoint{.x_q6 = too_far, .y_q6 = 0},
        os::ui::UiAction::activate);
    assert(!invalid_point);
    expect_ui_error(invalid_point.error(), os::ui::errors::invalid_input_point);

    auto malformed = tree.renderer_snapshot();
    assert(malformed.count >= 2U);
    malformed.nodes[1].id = malformed.nodes[0].id;
    auto invalid_snapshot = os::ui::route_pointer_action(
        malformed,
        point(60U, 230U),
        os::ui::UiAction::activate);
    assert(!invalid_snapshot);
    expect_ui_error(
        invalid_snapshot.error(),
        os::ui::errors::invalid_input_snapshot);

    return 0;
}
