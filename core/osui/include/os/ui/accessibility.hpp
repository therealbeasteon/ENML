#pragma once

#include <cstdint>

#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/core/strong_id.hpp>
#include <os/ui/tree.hpp>

namespace os::ui {

struct AccessibilitySessionIdTag;
using AccessibilitySessionId = os::core::StrongId<AccessibilitySessionIdTag, std::uint64_t>;

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

// Transport-facing wrappers bind one tree snapshot/action to the runtime
// session that owns the tree. A privileged service must not be able to replay a
// perfectly valid revision/node pair against a different application's tree.
struct AccessibilitySessionSnapshot final {
    AccessibilitySessionId session {};
    AccessibilityServiceSnapshot snapshot {};
};

struct AccessibilitySessionActionRequest final {
    AccessibilitySessionId session {};
    AccessibilityActionRequest request {};
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
// transport. Wire code should pass the supervisor-resolved peer identity and
// the runtime-minted session id here rather than duplicating access/session
// policy in a protocol handler. The application still owns its SemanticTree;
// only the trusted accessibility principal may access this exact session.
class AccessibilityBridgeAuthority final {
public:
    AccessibilityBridgeAuthority(
        SemanticTree& tree,
        os::core::PrincipalId trusted_accessibility_principal,
        AccessibilitySessionId session) noexcept
        : tree_(&tree),
          trusted_accessibility_principal_(trusted_accessibility_principal),
          session_(session) {}

    [[nodiscard]] bool valid() const noexcept {
        return tree_ != nullptr && tree_->valid() &&
            os::core::valid_principal(trusted_accessibility_principal_) &&
            session_.value() != 0U;
    }

    [[nodiscard]] AccessibilitySessionId session() const noexcept { return session_; }

    [[nodiscard]] os::core::Result<AccessibilitySessionSnapshot> snapshot(
        os::core::PeerIdentity caller) const noexcept;

    [[nodiscard]] os::core::Result<UiEvent> dispatch(
        os::core::PeerIdentity caller,
        AccessibilitySessionActionRequest request) noexcept;

private:
    [[nodiscard]] bool caller_allowed(os::core::PeerIdentity caller) const noexcept;

    SemanticTree* tree_ {nullptr};
    os::core::PrincipalId trusted_accessibility_principal_ {};
    AccessibilitySessionId session_ {};
};

} // namespace os::ui
