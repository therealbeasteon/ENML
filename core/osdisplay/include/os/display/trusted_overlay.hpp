#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/display/types.hpp>

namespace os::display {

// This is not application drawable content. It is a compositor/backend
// instruction derived from already-authorized surface roles and emitted in a
// separate compositor-owned pass after client buffers are composed. The
// concrete ENML trust mark remains renderer/theme policy and is intentionally
// not frozen into this protocol as vendor-looking pixels.
struct TrustedOverlayEntry final {
    SurfaceId surface {};
    TrustedPresentation presentation {TrustedPresentation::none};
    Rect bounds {};
    std::uint64_t frame_sequence {0U};
};

struct TrustedOverlaySnapshot final {
    std::array<TrustedOverlayEntry, max_surfaces> entries {};
    std::size_t count {0U};
};

// Filters only visible, framed, compositor-attributed system presentation.
// Applications cannot create an entry by drawing a lookalike because ordinary
// application/popup SceneEntry values carry TrustedPresentation::none.
[[nodiscard]] TrustedOverlaySnapshot build_trusted_overlay_snapshot(
    const SceneSnapshot& scene) noexcept;

} // namespace os::display
