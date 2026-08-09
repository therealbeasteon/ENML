#pragma once

#include <cstdint>

#include <os/core/identity.hpp>
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

// In-process authorization seam for the eventual privileged accessibility
// transport. Wire code should pass the supervisor-resolved peer identity here
// rather than duplicating access policy in a protocol handler. The application
// still owns its SemanticTree; only the trusted accessibility principal may
// request snapshots/actions through this privileged bridge.
class AccessibilityBridgeAuthority final {
public:
    AccessibilityBridgeAuthority(
        SemanticTree& tree,
        os::core::PrincipalId trusted_accessibility_principal) noexcept
        : tree_(&tree), trusted_accessibility_principal_(trusted_accessibility_principal) {}

    [[nodiscard]] bool valid() const noexcept {
        return tree_ != nullptr && tree_->valid() &&
            os::core::valid_principal(trusted_accessibility_principal_);
    }

    [[nodiscard]] os::core::Result<AccessibilityServiceSnapshot> snapshot(
        os::core::PeerIdentity caller) const noexcept;

    [[nodiscard]] os::core::Result<UiEvent> dispatch(
        os::core::PeerIdentity caller,
        AccessibilityActionRequest request) noexcept;

private:
    [[nodiscard]] bool caller_allowed(os::core::PeerIdentity caller) const noexcept;

    SemanticTree* tree_ {nullptr};
    os::core::PrincipalId trusted_accessibility_principal_ {};
};

} // namespace os::ui
