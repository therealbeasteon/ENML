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

    // Whether another principal's surface covered this surface when the event
    // was taken. Both are carried on the hit rather than only being checked,
    // so a downstream policy can reason about them and so a test can assert
    // what the compositor observed rather than only that it refused.
    //
    // partially_obscured means an overlapping surface covered some part of the
    // target - enough to have altered what the user believed they were
    // pressing, for instance by covering a label next to the control. Both are
    // false on any hit this compositor returns; see hit_test_input.
    bool obscured_at_point {false};
    bool partially_obscured {false};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return valid_display_object_value(surface.value()) &&
            os::core::valid_peer_identity(owner) && surface_size.valid() &&
            frame_sequence != 0U && local_x >= 0 && local_y >= 0 &&
            static_cast<std::uint32_t>(local_x) < surface_size.width &&
            static_cast<std::uint32_t>(local_y) < surface_size.height;
    }
};

} // namespace os::display
