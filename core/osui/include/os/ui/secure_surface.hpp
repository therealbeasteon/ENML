#pragma once

#include <cstdint>

#include <os/ui/identity.hpp>

namespace os::ui {

enum class SurfaceOwnerKind : std::uint8_t {
    application = 0U,
    trusted_service = 1U,
    platform = 2U,
};

enum class CapturePolicy : std::uint8_t {
    allowed = 0U,
    obscure = 1U,
    denied = 2U,
};

struct SecureSurfacePolicy final {
    PlaneRole plane {PlaneRole::content};
    SurfaceOwnerKind owner {SurfaceOwnerKind::application};
    bool trusted_attribution {false};
    CapturePolicy capture {CapturePolicy::allowed};
};

[[nodiscard]] constexpr bool secure_surface_policy_valid(const SecureSurfacePolicy& policy) noexcept {
    if (policy.plane != PlaneRole::secure) return true;
    if (policy.owner != SurfaceOwnerKind::platform && policy.owner != SurfaceOwnerKind::trusted_service) {
        return false;
    }
    if (!policy.trusted_attribution) return false;
    return policy.capture == CapturePolicy::denied;
}

[[nodiscard]] constexpr bool application_can_present(const SecureSurfacePolicy& policy) noexcept {
    return policy.owner == SurfaceOwnerKind::application && policy.plane != PlaneRole::secure;
}

} // namespace os::ui
