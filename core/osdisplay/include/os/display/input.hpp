#pragma once

#include <cstdint>

#include <os/core/identity.hpp>
#include <os/display/types.hpp>

namespace os::display {

// Privileged compositor result for one global display-space input point.
// The compositor, not an application, chooses the target surface. Downstream
// input delivery should forward only surface-local coordinates and the exact
// owner/surface/frame identity needed for authorization; applications do not
// need global screen position, scene ordering, or another app's geometry.
struct SurfaceInputHit final {
    SurfaceId surface {};
    os::core::PeerIdentity owner {};
    SurfaceRole role {SurfaceRole::application};
    PixelSize surface_size {};
    std::uint64_t frame_sequence {0U};
    std::int32_t local_x {0};
    std::int32_t local_y {0};
    TrustedPresentation trusted_presentation {TrustedPresentation::none};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return valid_display_object_value(surface.value()) &&
            os::core::valid_peer_identity(owner) && surface_size.valid() &&
            frame_sequence != 0U && local_x >= 0 && local_y >= 0 &&
            static_cast<std::uint32_t>(local_x) < surface_size.width &&
            static_cast<std::uint32_t>(local_y) < surface_size.height;
    }
};

} // namespace os::display
