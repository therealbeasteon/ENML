#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/ui/types.hpp>

namespace os::ui {

struct RendererSnapshot final {
    std::uint64_t revision {0U};
    std::array<UiNodeDescriptor, max_ui_nodes> nodes {};
    std::size_t count {0U};
};

// Dirty metadata is a bounded optimization hint. If removals overflow before a
// consumer drains them, full_resync_required is set and RendererSnapshot is the
// authoritative recovery path.
struct RendererDelta final {
    std::uint64_t revision {0U};
    std::array<UiNodeId, max_ui_nodes> changed {};
    std::size_t changed_count {0U};
    std::array<UiNodeId, max_ui_nodes> removed {};
    std::size_t removed_count {0U};
    bool full_resync_required {false};
};

class SemanticTree final {
public:
    explicit SemanticTree(LogicalRect root_bounds) noexcept;

    SemanticTree(const SemanticTree&) = delete;
    SemanticTree& operator=(const SemanticTree&) = delete;

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] UiNodeId root() const noexcept { return root_id_; }
    [[nodiscard]] std::size_t node_count() const noexcept { return node_count_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    [[nodiscard]] os::core::Result<UiNodeDescriptor> add(
        UiNodeId parent,
        const UiNodeSpec& spec) noexcept;

    [[nodiscard]] os::core::Result<void> remove_subtree(UiNodeId node) noexcept;

    [[nodiscard]] os::core::Result<UiNodeDescriptor> lookup(UiNodeId node) const noexcept;

    [[nodiscard]] os::core::Result<void> set_bounds(
        UiNodeId node,
        LogicalRect bounds) noexcept;

    [[nodiscard]] os::core::Result<void> set_state(
        UiNodeId node,
        UiNodeState state) noexcept;

    [[nodiscard]] os::core::Result<void> set_label(
        UiNodeId node,
        SemanticText label) noexcept;

    [[nodiscard]] os::core::Result<void> set_style(
        UiNodeId node,
        StyleTokenId style) noexcept;

    [[nodiscard]] os::core::Result<void> focus(UiNodeId node) noexcept;
    void clear_focus() noexcept;
    [[nodiscard]] os::core::Result<UiNodeId> focused_node() const noexcept;

    [[nodiscard]] os::core::Result<UiEvent> dispatch_action(
        UiNodeId node,
        UiAction action) const noexcept;

    [[nodiscard]] AccessibilitySnapshot accessibility_snapshot() const noexcept;
    [[nodiscard]] RendererSnapshot renderer_snapshot() const noexcept;
    [[nodiscard]] RendererDelta take_renderer_delta() noexcept;

private:
    struct Slot final {
        bool occupied {false};
        bool dirty {false};
        UiNodeDescriptor descriptor {};
    };

    [[nodiscard]] Slot* find(UiNodeId node) noexcept;
    [[nodiscard]] const Slot* find(UiNodeId node) const noexcept;
    [[nodiscard]] std::size_t child_count(UiNodeId parent) const noexcept;
    [[nodiscard]] bool is_descendant_of(const Slot& candidate, UiNodeId ancestor) const noexcept;
    [[nodiscard]] bool effectively_visible(const Slot& slot) const noexcept;
    [[nodiscard]] UiNodeId accessible_parent(UiNodeId node) const noexcept;

    [[nodiscard]] static bool role_can_have_children(UiRole role) noexcept;
    [[nodiscard]] static bool role_valid(UiRole role) noexcept;
    [[nodiscard]] static bool actions_valid(UiRole role, UiActionMask actions) noexcept;
    [[nodiscard]] static bool state_valid(
        UiRole role,
        UiActionMask actions,
        const UiNodeState& state) noexcept;
    [[nodiscard]] static bool label_valid_for_role(
        UiRole role,
        const SemanticText& label,
        bool accessibility_hidden) noexcept;

    void bump_revision() noexcept;
    void mark_dirty(Slot& slot) noexcept;
    void record_removed(UiNodeId id) noexcept;

    std::array<Slot, max_ui_nodes> slots_ {};
    std::array<UiNodeId, max_ui_nodes> removed_dirty_ {};
    UiNodeId root_id_ {};
    UiNodeId focused_ {};
    std::uint32_t next_id_ {1U};
    std::size_t node_count_ {0U};
    std::size_t removed_dirty_count_ {0U};
    std::uint64_t revision_ {0U};
    bool dirty_overflow_ {false};
    bool valid_ {false};
};

} // namespace os::ui
