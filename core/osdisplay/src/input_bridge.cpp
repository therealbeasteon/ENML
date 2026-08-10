#include <os/display/input_bridge.hpp>

#include <os/core/error.hpp>
#include <os/display/error.hpp>

namespace os::display {

bool InputBridgeAuthority::caller_allowed(os::core::PeerIdentity caller) const noexcept {
    return valid() && os::core::valid_peer_identity(caller) &&
        caller.principal == trusted_input_principal_;
}

os::core::Result<SurfaceInputHit> InputBridgeAuthority::hit_test(
    os::core::PeerIdentity caller,
    std::int32_t global_x,
    std::int32_t global_y) const noexcept {
    if (!valid()) return display_error(errors::invalid_configuration);
    if (!caller_allowed(caller)) return display_error(errors::input_authority_denied);
    return compositor_->hit_test_input(global_x, global_y);
}

os::core::Result<void> InputBridgeAuthority::validate_before_delivery(
    os::core::PeerIdentity caller,
    const SurfaceInputHit& hit) const noexcept {
    if (!valid()) return display_error(errors::invalid_configuration);
    if (!caller_allowed(caller)) return display_error(errors::input_authority_denied);
    return compositor_->validate_input_hit(hit);
}

} // namespace os::display
