#include <os/ui/tree.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <os/ui/error.hpp>

namespace os::ui {
namespace {

inline constexpr UiActionMask known_actions =
    action_mask(UiAction::activate) |
    action_mask(UiAction::focus) |
    action_mask(UiAction::toggle) |
    action_mask(UiAction::set_text) |
    action_mask(UiAction::select);

[[nodiscard]] constexpr bool single_known_action(UiAction action) noexcept {
    const UiActionMask value = action_mask(action);
    return value != 0U && (value & known_actions) == value &&
        (value & static_cast<UiActionMask>(value - 1U)) == 0U;
}

[[nodiscard]] constexpr bool interactive_role(UiRole role) noexcept {
    return role == UiRole::button || role == UiRole::toggle ||
        role == UiRole::text_field || role == UiRole::list_item;
}

} // namespace

SemanticTree::SemanticTree(LogicalRect root_bounds) noexcept {
    if (!root_bounds.bounded()) return;

    root_id_ = UiNodeId{1U};
    next_id_ = 2U;
    slots_[0] = Slot{
        .occupied = true,
        .descriptor = UiNodeDescriptor{
            .id = root_id_,
            .parent = {},
            .depth = 0U,
            .spec = UiNodeSpec{
                .role = UiRole::root,
                .bounds = root_bounds,
                .actions = 0U,
                .state = {},
                .style = {},
                .label = {},
                .accessibility_hidden = false,
            },
        },
    };
    node_count_ = 1U;
    valid_ = true;
}

bool SemanticTree::role_valid(UiRole role) noexcept {
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

bool SemanticTree::role_can_have_children(UiRole role) noexcept {
    switch (role) {
    case UiRole::root:
    case UiRole::container:
    case UiRole::button:
    case UiRole::toggle:
    case UiRole::list:
    case UiRole::list_item:
        return true;
    case UiRole::text:
    case UiRole::image:
    case UiRole::text_field:
        return false;
    }
    return false;
}

bool SemanticTree::actions_valid(UiRole role, UiActionMask actions) noexcept {
    if ((actions & static_cast<UiActionMask>(~known_actions)) != 0U) return false;

    UiActionMask allowed = 0U;
    switch (role) {
    case UiRole::root:
    case UiRole::container:
    case UiRole::text:
    case UiRole::image:
    case UiRole::list:
        allowed = 0U;
        break;
    case UiRole::button:
        allowed = action_mask(UiAction::activate) | action_mask(UiAction::focus);
        break;
    case UiRole::toggle:
        allowed = action_mask(UiAction::activate) | action_mask(UiAction::focus) |
            action_mask(UiAction::toggle);
        break;
    case UiRole::text_field:
        allowed = action_mask(UiAction::focus) | action_mask(UiAction::set_text);
        break;
    case UiRole::list_item:
        allowed = action_mask(UiAction::activate) | action_mask(UiAction::focus) |
            action_mask(UiAction::select);
        break;
    }
    return (actions & static_cast<UiActionMask>(~allowed)) == 0U;
}

bool SemanticTree::state_valid(
    UiRole role,
    UiActionMask actions,
    const UiNodeState& state) noexcept {
    if (state.focused &&
        (!state.visible || !state.enabled || !has_action(actions, UiAction::focus))) {
        return false;
    }
    if (state.pressed && (!state.visible || !state.enabled)) return false;
    if (state.checked && role != UiRole::toggle) return false;
    if (state.selected && role != UiRole::list_item) return false;
    if (state.pressed && role != UiRole::button && role != UiRole::toggle &&
        role != UiRole::list_item) return false;
    return true;
}

bool SemanticTree::label_valid_for_role(
    UiRole role,
    const SemanticText& label,
    bool accessibility_hidden) noexcept {
    if (!semantic_text_valid(label)) return false;
    if (interactive_role(role) && !accessibility_hidden && label.empty()) return false;
    return true;
}

SemanticTree::Slot* SemanticTree::find(UiNodeId node) noexcept {
    if (node.value() == 0U) return nullptr;
    for (auto& slot : slots_) {
        if (slot.occupied && slot.descriptor.id == node) return &slot;
    }
    return nullptr;
}

const SemanticTree::Slot* SemanticTree::find(UiNodeId node) const noexcept {
    if (node.value() == 0U) return nullptr;
    for (const auto& slot : slots_) {
        if (slot.occupied && slot.descriptor.id == node) return &slot;
    }
    return nullptr;
}

std::size_t SemanticTree::child_count(UiNodeId parent) const noexcept {
    std::size_t count = 0U;
    for (const auto& slot : slots_) {
        if (slot.occupied && slot.descriptor.parent == parent) ++count;
    }
    return count;
}

bool SemanticTree::is_descendant_of(const Slot& candidate, UiNodeId ancestor) const noexcept {
    UiNodeId current = candidate.descriptor.parent;
    for (std::uint8_t depth = 0U; depth <= max_ui_depth; ++depth) {
        if (current.value() == 0U) return false;
        if (current == ancestor) return true;
        const Slot* parent = find(current);
        if (parent == nullptr) return false;
        current = parent->descriptor.parent;
    }
    return false;
}

bool SemanticTree::effectively_visible(const Slot& slot) const noexcept {
    const Slot* current = &slot;
    for (std::uint8_t depth = 0U; depth <= max_ui_depth; ++depth) {
        if (!current->descriptor.spec.state.visible) return false;
        const UiNodeId parent = current->descriptor.parent;
        if (parent.value() == 0U) return true;
        current = find(parent);
        if (current == nullptr) return false;
    }
    return false;
}

UiNodeId SemanticTree::accessible_parent(UiNodeId node) const noexcept {
    const Slot* current = find(node);
    if (current == nullptr) return {};
    UiNodeId parent_id = current->descriptor.parent;
    for (std::uint8_t depth = 0U; depth <= max_ui_depth; ++depth) {
        if (parent_id.value() == 0U) return {};
        const Slot* parent = find(parent_id);
        if (parent == nullptr) return {};
        if (effectively_visible(*parent) && !parent->descriptor.spec.accessibility_hidden) {
            return parent_id;
        }
        parent_id = parent->descriptor.parent;
    }
    return {};
}

os::core::Result<UiNodeDescriptor> SemanticTree::add(
    UiNodeId parent,
    const UiNodeSpec& spec) noexcept {
    if (!valid_) return ui_error(errors::invalid_tree);
    if (!role_valid(spec.role) || spec.role == UiRole::root) {
        return ui_error(errors::invalid_role);
    }
    if (!spec.bounds.bounded()) return ui_error(errors::invalid_bounds);
    if (!actions_valid(spec.role, spec.actions)) return ui_error(errors::invalid_action);
    if (spec.accessibility_hidden && spec.actions != 0U) {
        return ui_error(errors::invalid_action);
    }
    if (!label_valid_for_role(spec.role, spec.label, spec.accessibility_hidden)) {
        return ui_error(errors::invalid_text);
    }
    if (!state_valid(spec.role, spec.actions, spec.state)) {
        return ui_error(errors::invalid_state);
    }

    Slot* parent_slot = find(parent);
    if (parent_slot == nullptr || !role_can_have_children(parent_slot->descriptor.spec.role)) {
        return ui_error(errors::invalid_parent);
    }
    if (child_count(parent) >= max_ui_children_per_node) {
        return ui_error(errors::child_limit);
    }
    if (parent_slot->descriptor.depth >= max_ui_depth) {
        return ui_error(errors::depth_limit);
    }
    if (node_count_ >= max_ui_nodes) return ui_error(errors::node_limit);
    if (next_id_ == 0U) return ui_error(errors::id_exhausted);

    if (spec.state.focused && !effectively_visible(*parent_slot)) {
        return ui_error(errors::invalid_state);
    }

    Slot* available = nullptr;
    for (auto& slot : slots_) {
        if (!slot.occupied) {
            available = &slot;
            break;
        }
    }
    if (available == nullptr) return ui_error(errors::node_limit);

    const UiNodeId id{next_id_};
    if (next_id_ == std::numeric_limits<std::uint32_t>::max()) {
        next_id_ = 0U;
    } else {
        ++next_id_;
    }

    UiNodeSpec stored_spec = spec;
    if (stored_spec.state.focused) clear_focus();
    *available = Slot{
        .occupied = true,
        .descriptor = UiNodeDescriptor{
            .id = id,
            .parent = parent,
            .depth = static_cast<std::uint8_t>(parent_slot->descriptor.depth + 1U),
            .spec = stored_spec,
        },
    };
    ++node_count_;
    if (stored_spec.state.focused) focused_ = id;
    return available->descriptor;
}

os::core::Result<void> SemanticTree::remove_subtree(UiNodeId node) noexcept {
    if (!valid_) return ui_error(errors::invalid_tree);
    Slot* target = find(node);
    if (target == nullptr) return ui_error(errors::invalid_node);
    if (node == root_id_) return ui_error(errors::root_immutable);

    std::array<bool, max_ui_nodes> remove{};
    std::size_t removed = 0U;
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        const auto& slot = slots_[index];
        if (!slot.occupied) continue;
        if (slot.descriptor.id == node || is_descendant_of(slot, node)) {
            remove[index] = true;
            ++removed;
        }
    }

    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        if (!remove[index]) continue;
        if (slots_[index].descriptor.id == focused_) focused_ = {};
        slots_[index] = Slot{};
    }
    node_count_ -= removed;
    return {};
}

os::core::Result<UiNodeDescriptor> SemanticTree::lookup(UiNodeId node) const noexcept {
    if (!valid_) return ui_error(errors::invalid_tree);
    const Slot* slot = find(node);
    if (slot == nullptr) return ui_error(errors::invalid_node);
    return slot->descriptor;
}

os::core::Result<void> SemanticTree::set_bounds(UiNodeId node, LogicalRect bounds) noexcept {
    if (!valid_) return ui_error(errors::invalid_tree);
    if (!bounds.bounded()) return ui_error(errors::invalid_bounds);
    Slot* slot = find(node);
    if (slot == nullptr) return ui_error(errors::invalid_node);
    slot->descriptor.spec.bounds = bounds;
    return {};
}

os::core::Result<void> SemanticTree::set_state(UiNodeId node, UiNodeState state) noexcept {
    if (!valid_) return ui_error(errors::invalid_tree);
    Slot* slot = find(node);
    if (slot == nullptr) return ui_error(errors::invalid_node);
    if (node == root_id_) return ui_error(errors::root_immutable);
    if (!state_valid(slot->descriptor.spec.role, slot->descriptor.spec.actions, state)) {
        return ui_error(errors::invalid_state);
    }

    if (state.focused) {
        UiNodeId parent = slot->descriptor.parent;
        for (std::uint8_t depth = 0U; depth <= max_ui_depth; ++depth) {
            if (parent.value() == 0U) break;
            const Slot* parent_slot = find(parent);
            if (parent_slot == nullptr || !parent_slot->descriptor.spec.state.visible) {
                return ui_error(errors::invalid_state);
            }
            parent = parent_slot->descriptor.parent;
        }
        clear_focus();
        focused_ = node;
    } else if (focused_ == node) {
        focused_ = {};
    }

    slot->descriptor.spec.state = state;
    return {};
}

os::core::Result<void> SemanticTree::set_label(UiNodeId node, SemanticText label) noexcept {
    if (!valid_) return ui_error(errors::invalid_tree);
    Slot* slot = find(node);
    if (slot == nullptr) return ui_error(errors::invalid_node);
    if (!label_valid_for_role(
            slot->descriptor.spec.role,
            label,
            slot->descriptor.spec.accessibility_hidden)) {
        return ui_error(errors::invalid_text);
    }
    slot->descriptor.spec.label = label;
    return {};
}

os::core::Result<void> SemanticTree::set_style(UiNodeId node, StyleTokenId style) noexcept {
    if (!valid_) return ui_error(errors::invalid_tree);
    Slot* slot = find(node);
    if (slot == nullptr) return ui_error(errors::invalid_node);
    slot->descriptor.spec.style = style;
    return {};
}

os::core::Result<void> SemanticTree::focus(UiNodeId node) noexcept {
    if (!valid_) return ui_error(errors::invalid_tree);
    Slot* slot = find(node);
    if (slot == nullptr) return ui_error(errors::invalid_node);
    if (!has_action(slot->descriptor.spec.actions, UiAction::focus) ||
        !slot->descriptor.spec.state.enabled || !effectively_visible(*slot)) {
        return ui_error(errors::invalid_state);
    }

    clear_focus();
    slot->descriptor.spec.state.focused = true;
    focused_ = node;
    return {};
}

void SemanticTree::clear_focus() noexcept {
    if (focused_.value() != 0U) {
        Slot* previous = find(focused_);
        if (previous != nullptr) previous->descriptor.spec.state.focused = false;
    }
    focused_ = {};
}

os::core::Result<UiNodeId> SemanticTree::focused_node() const noexcept {
    if (!valid_) return ui_error(errors::invalid_tree);
    if (focused_.value() == 0U || find(focused_) == nullptr) {
        return ui_error(errors::no_focus);
    }
    return focused_;
}

os::core::Result<UiEvent> SemanticTree::dispatch_action(
    UiNodeId node,
    UiAction action) const noexcept {
    if (!valid_) return ui_error(errors::invalid_tree);
    const Slot* slot = find(node);
    if (slot == nullptr) return ui_error(errors::invalid_node);
    if (!single_known_action(action) ||
        !has_action(slot->descriptor.spec.actions, action)) {
        return ui_error(errors::invalid_action);
    }
    if (!slot->descriptor.spec.state.enabled || !effectively_visible(*slot)) {
        return ui_error(errors::invalid_state);
    }
    return UiEvent{.target = node, .action = action};
}

AccessibilitySnapshot SemanticTree::accessibility_snapshot() const noexcept {
    AccessibilitySnapshot snapshot{};
    if (!valid_) return snapshot;

    std::array<const Slot*, max_ui_nodes> ordered{};
    std::size_t count = 0U;
    for (const auto& slot : slots_) {
        if (!slot.occupied || slot.descriptor.spec.accessibility_hidden ||
            !effectively_visible(slot)) continue;
        ordered[count] = &slot;
        ++count;
    }

    for (std::size_t index = 1U; index < count; ++index) {
        const Slot* current = ordered[index];
        std::size_t position = index;
        while (position > 0U &&
               ordered[position - 1U]->descriptor.id.value() > current->descriptor.id.value()) {
            ordered[position] = ordered[position - 1U];
            --position;
        }
        ordered[position] = current;
    }

    snapshot.count = count;
    for (std::size_t index = 0U; index < count; ++index) {
        const UiNodeDescriptor& descriptor = ordered[index]->descriptor;
        snapshot.nodes[index] = AccessibilityNode{
            .id = descriptor.id,
            .parent = accessible_parent(descriptor.id),
            .role = descriptor.spec.role,
            .bounds = descriptor.spec.bounds,
            .state = descriptor.spec.state,
            .actions = descriptor.spec.actions,
            .label = descriptor.spec.label,
        };
    }
    return snapshot;
}

} // namespace os::ui
