#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/ui/tree.hpp>

namespace os::ui {

// Platform accessibility consumes the semantic projection together with the
// exact tree revision that produced it. The revision is intentionally outside
// individual nodes: one fixed-capacity snapshot is one coherent accessibility
// state, not a bag of independently versioned objects.
struct AccessibilityServiceSnapshot final {
    std::uint64_t revision {0U};
    AccessibilitySnapshot semantic {};
};

struct AccessibilityActionRequest final {
    std::uint64_t snapshot_revision {0U};
    UiNodeId target {};
    UiAction action {UiAction::activate};
};

// Captures a coherent, bounded semantic accessibility state. No framebuffer
// inspection/OCR or renderer pixel access is required.
[[nodiscard]] os::core::Result<AccessibilityServiceSnapshot>
accessibility_service_snapshot(const SemanticTree& tree) noexcept;

// Re-authorizes one action against the current tree and rejects requests made
// from a stale accessibility snapshot. Text replacement is deliberately not
// accepted yet because a bounded editable-text payload/IME contract has not
// been frozen; activate/focus/toggle/select remain semantic actions.
[[nodiscard]] os::core::Result<UiEvent> dispatch_accessibility_action(
    SemanticTree& tree,
    AccessibilityActionRequest request) noexcept;

} // namespace os::ui
