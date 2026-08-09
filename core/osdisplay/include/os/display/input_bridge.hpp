#pragma once

#include <cstdint>

#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/display/compositor.hpp>

namespace os::display {

// In-process authority seam for the future privileged system.input transport.
// The supervisor-resolved caller identity is checked here; applications cannot
// choose a SurfaceId or ask the compositor to disclose arbitrary scene hits.
// A later RPC service should wrap this class rather than reimplementing the
// authorization rules in wire-protocol code.
class InputBridgeAuthority final {
public:
    InputBridgeAuthority(
        Compositor& compositor,
        os::core::PrincipalId trusted_input_principal) noexcept
        : compositor_(&compositor), trusted_input_principal_(trusted_input_principal) {}

    [[nodiscard]] bool valid() const noexcept {
        return compositor_ != nullptr && compositor_->valid() &&
            os::core::valid_principal(trusted_input_principal_);
    }

    [[nodiscard]] os::core::Result<SurfaceInputHit> hit_test(
        os::core::PeerIdentity caller,
        std::int32_t global_x,
        std::int32_t global_y) const noexcept;

    [[nodiscard]] os::core::Result<void> validate_before_delivery(
        os::core::PeerIdentity caller,
        const SurfaceInputHit& hit) const noexcept;

private:
    [[nodiscard]] bool caller_allowed(os::core::PeerIdentity caller) const noexcept;

    Compositor* compositor_ {nullptr};
    os::core::PrincipalId trusted_input_principal_ {};
};

} // namespace os::display
