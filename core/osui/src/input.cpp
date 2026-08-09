#include <os/ui/input.hpp>

#include <cstddef>
#include <cstdint>

#include <os/ui/error.hpp>

namespace os::ui {
namespace {

[[nodiscard]] constexpr bool role_valid(UiRole role) noexcept {
    switch (role) {
    case UiRole::root:
    case UiRole::container:
    case UiRole::text:
    case UiRole::image:
    case UiRole::button:
    case UiRole::toggle:
    case UiRole::text_field:
    case UiRole::list:
    case UiRole::list_item:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool pointer_action_valid(UiAction action) noexcept {
    switch (action) {
    case UiAction::activate:
    case UiAction::focus:
    case UiAction::toggle:
    case UiAction::select:
        return true;
    case UiAction::set_text:
        return false;
    }
    return false;
}

[[nodiscard]] constexpr UiActionMask known_actions_mask() noexcept {
    return action_mask(UiAction::activate) |
        action_mask(UiAction::focus) |
        action_mask(UiAction::toggle) |
        action_mask(UiAction::set_text) |
        action_mask(UiAction::select);
}

[[nodiscard]] constexpr bool point_valid(LogicalPoint point) noexcept {
    const auto bound = static_cast<std::int64_t>(max_logical_dimension_q6);
    const auto x = static_cast<std::int64_t>(point.x_q6);
    const auto y = static_cast<std::int64_t>(point.y_q6);
    return x >= -bound && x <= bound && y >= -bound && y <= bound;
}

[[nodiscard]] constexpr bool point_inside(
    const LogicalRect& rect,
    LogicalPoint point) noexcept {
    const auto left = static_cast<std::int64_t>(rect.x_q6);
    const auto top = static_cast<std::int64_t>(rect.y_q6);
    const auto right = left + static_cast<std::int64_t>(rect.width_q6);
    const auto bottom = top + static_cast<std::int64_t>(rect.height_q6);
    const auto x = static_cast<std::int64_t>(point.x_q6);
    const auto y = static_cast<std::int64_t>(point.y_q6);
    return x >= left && x < right && y >= top && y < bottom;
}

[[nodiscard]] const UiNodeDescriptor* find_node(
    const RendererSnapshot& snapshot,
    UiNodeId id) noexcept {
    if (id.value() == 0U) return nullptr;
    for (std::size_t index = 0U; index < snapshot.count; ++index) {
        if (snapshot.nodes[index].id == id) return &snapshot.nodes[index];
    }
    return nullptr;
}

[[nodiscard]] bool snapshot_valid(const RendererSnapshot& snapshot) noexcept {
    if (snapshot.count == 0U || snapshot.count > max_ui_nodes) return false;

    std::size_t root_count = 0U;
    for (std::size_t index = 0U; index < snapshot.count; ++index) {
        const UiNodeDescriptor& node = snapshot.nodes[index];
        if (node.id.value() == 0U || !role_valid(node.spec.role) ||
            node.depth > max_ui_depth || !node.spec.bounds.bounded() ||
            (node.spec.actions & ~known_actions_mask()) != 0U) {
            return false;
        }
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (snapshot.nodes[earlier].id == node.id) return false;
        }

        if (node.spec.role == UiRole::root) {
            ++root_count;
            if (node.parent.value() != 0U || node.depth != 0U) return false;
            continue;
        }

        if (node.parent.value() == 0U || node.depth == 0U) return false;
        const UiNodeDescriptor* parent = find_node(snapshot, node.parent);
        if (parent == nullptr || parent->depth + 1U != node.depth) return false;
    }
    if (root_count != 1U) return false;

    for (std::size_t index = 0U; index < snapshot.count; ++index) {
        const UiNodeDescriptor* current = &snapshot.nodes[index];
        for (std::uint8_t steps = 0U; steps <= max_ui_depth; ++steps) {
            if (current->spec.role == UiRole::root) break;
            current = find_node(snapshot, current->parent);
            if (current == nullptr) return false;
            if (steps == max_ui_depth) return false;
        }
        if (current->spec.role != UiRole::root) return false;
    }
    return true;
}

[[nodiscard]] bool effectively_visible(
    const RendererSnapshot& snapshot,
    const UiNodeDescriptor& node) noexcept {
    const UiNodeDescriptor* current = &node;
    for (std::uint8_t steps = 0U; steps <= max_ui_depth; ++steps) {
        if (!current->spec.state.visible) return false;
        if (current->spec.role == UiRole::root) return true;
        current = find_node(snapshot, current->parent);
        if (current == nullptr) return false;
    }
    return false;
}

[[nodiscard]] const UiNodeDescriptor* topmost_hit(
    const RendererSnapshot& snapshot,
    LogicalPoint point) noexcept {
    const UiNodeDescriptor* topmost = nullptr;
    for (std::size_t index = 0U; index < snapshot.count; ++index) {
        const UiNodeDescriptor& node = snapshot.nodes[index];
        if (!effectively_visible(snapshot, node) || !point_inside(node.spec.bounds, point)) {
            continue;
        }
        if (topmost == nullptr || node.depth > topmost->depth ||
            (node.depth == topmost->depth && node.id.value() > topmost->id.value())) {
            topmost = &node;
        }
    }
    return topmost;
}

} // namespace

os::core::Result<PointerRoute> route_pointer_action(
    const RendererSnapshot& snapshot,
    LogicalPoint point,
    UiAction action) noexcept {
    if (!point_valid(point)) return ui_error(errors::invalid_input_point);
    if (!pointer_action_valid(action)) return ui_error(errors::invalid_input_action);
    if (!snapshot_valid(snapshot)) return ui_error(errors::invalid_input_snapshot);

    const UiNodeDescriptor* current = topmost_hit(snapshot, point);
    if (current == nullptr) return ui_error(errors::input_no_target);

    for (std::uint8_t steps = 0U; steps <= max_ui_depth; ++steps) {
        if (!point_inside(current->spec.bounds, point)) {
            return ui_error(errors::input_no_target);
        }
        if (current->spec.state.enabled && has_action(current->spec.actions, action)) {
            return PointerRoute{
                .target = current->id,
                .role = current->spec.role,
                .action = action,
                .bounds = current->spec.bounds,
            };
        }
        if (current->spec.role == UiRole::root) break;
        current = find_node(snapshot, current->parent);
        if (current == nullptr) return ui_error(errors::invalid_input_snapshot);
    }

    return ui_error(errors::input_no_target);
}

os::core::Result<UiEvent> dispatch_pointer_action(
    SemanticTree& tree,
    LogicalPoint point,
    UiAction action) noexcept {
    if (!tree.valid()) return ui_error(errors::invalid_tree);

    auto route = route_pointer_action(tree.renderer_snapshot(), point, action);
    if (!route) return route.error();

    if (action == UiAction::focus) {
        auto focused = tree.focus(route.value().target);
        if (!focused) return focused.error();
        return UiEvent{.target = route.value().target, .action = action};
    }
    return tree.dispatch_action(route.value().target, action);
}

} // namespace os::ui
