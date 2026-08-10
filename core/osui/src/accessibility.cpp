#include <os/ui/accessibility.hpp>

#include <os/ui/error.hpp>

namespace os::ui {
namespace {

[[nodiscard]] constexpr bool accessibility_action_valid(UiAction action) noexcept {
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

} // namespace

os::core::Result<AccessibilityServiceSnapshot> accessibility_service_snapshot(
    const SemanticTree& tree) noexcept {
    if (!tree.valid()) return ui_error(errors::invalid_tree);
    const std::uint64_t revision = tree.revision();
    if (revision == 0U) return ui_error(errors::invalid_accessibility_snapshot);

    return AccessibilityServiceSnapshot{
        .revision = revision,
        .semantic = tree.accessibility_snapshot(),
    };
}

os::core::Result<UiEvent> dispatch_accessibility_action(
    SemanticTree& tree,
    AccessibilityActionRequest request) noexcept {
    if (!tree.valid()) return ui_error(errors::invalid_tree);
    if (request.snapshot_revision == 0U) {
        return ui_error(errors::invalid_accessibility_snapshot);
    }
    if (request.snapshot_revision != tree.revision()) {
        return ui_error(errors::stale_accessibility_snapshot);
    }
    if (!accessibility_action_valid(request.action)) {
        return ui_error(errors::invalid_accessibility_action);
    }

    // lookup() ensures stale/forged UiNodeId values do not become an action on
    // whatever currently occupies a slot. IDs are monotonic, but the explicit
    // check keeps this boundary independent of storage implementation details.
    auto target = tree.lookup(request.target);
    if (!target) return target.error();
    if (target.value().spec.accessibility_hidden) {
        return ui_error(errors::invalid_accessibility_action);
    }

    if (request.action == UiAction::focus) {
        auto focused = tree.focus(request.target);
        if (!focused) return focused.error();
        return UiEvent{.target = request.target, .action = request.action};
    }

    return tree.dispatch_action(request.target, request.action);
}

bool AccessibilityBridgeAuthority::caller_allowed(
    os::core::PeerIdentity caller) const noexcept {
    return valid() && os::core::valid_peer_identity(caller) &&
        caller.principal == trusted_accessibility_principal_;
}

os::core::Result<AccessibilitySessionSnapshot> AccessibilityBridgeAuthority::snapshot(
    os::core::PeerIdentity caller) const noexcept {
    if (!valid()) return ui_error(errors::invalid_tree);
    if (!caller_allowed(caller)) return ui_error(errors::accessibility_authority_denied);
    auto snapshot = accessibility_service_snapshot(*tree_);
    if (!snapshot) return snapshot.error();
    return AccessibilitySessionSnapshot{
        .session = session_,
        .snapshot = snapshot.value(),
    };
}

os::core::Result<UiEvent> AccessibilityBridgeAuthority::dispatch(
    os::core::PeerIdentity caller,
    AccessibilitySessionActionRequest request) noexcept {
    if (!valid()) return ui_error(errors::invalid_tree);
    if (!caller_allowed(caller)) return ui_error(errors::accessibility_authority_denied);
    if (request.session != session_) {
        return ui_error(errors::accessibility_session_mismatch);
    }
    return dispatch_accessibility_action(*tree_, request.request);
}

} // namespace os::ui
