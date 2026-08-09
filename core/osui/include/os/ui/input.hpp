#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/ui/tree.hpp>

namespace os::ui {

// Platform input adapters convert device-specific coordinates/events into this
// logical Q6 space before entering semantic UI. Applications never receive
// /dev/input descriptors, evdev structs, compositor hit-test internals or raw
// hardware coordinates through this boundary.
struct LogicalPoint final {
    std::int32_t x_q6 {0};
    std::int32_t y_q6 {0};
};

struct PointerRoute final {
    UiNodeId target {};
    UiRole role {UiRole::container};
    UiAction action {UiAction::activate};
    LogicalRect bounds {};
};

// Resolve one pointer-originated semantic action from an immutable tree
// snapshot. Hit testing follows the same deterministic semantic ordering as
// rendering: deeper nodes are above ancestors; equal-depth overlap is resolved
// by the larger monotonic UiNodeId, which is painted later by the current
// renderer. The topmost hit node owns the hit path: disabled/non-actionable
// overlays do not click through into unrelated lower siblings.
[[nodiscard]] os::core::Result<PointerRoute> route_pointer_action(
    const RendererSnapshot& snapshot,
    LogicalPoint point,
    UiAction action) noexcept;

// Convenience path for a live SemanticTree. Routing is computed from a fresh
// snapshot and then re-authorized by SemanticTree itself. Focus is the only
// pointer action that mutates tree focus directly; other actions go through
// the existing dispatch_action() validation path.
[[nodiscard]] os::core::Result<UiEvent> dispatch_pointer_action(
    SemanticTree& tree,
    LogicalPoint point,
    UiAction action) noexcept;

} // namespace os::ui
