#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/display/trusted_overlay.hpp>

namespace os::display {

// Private compositor/backend color. Applications cannot submit this structure
// or select the trust-mark palette through surface/frame APIs.
struct TrustedMarkRgba8 final {
    std::uint8_t red {0U};
    std::uint8_t green {0U};
    std::uint8_t blue {0U};
    std::uint8_t alpha {255U};

    [[nodiscard]] friend constexpr bool operator==(
        const TrustedMarkRgba8&,
        const TrustedMarkRgba8&) = default;
};

// Two-tone opaque treatment keeps the mark readable over arbitrary trusted
// client pixels without backdrop sampling. `foundation` is the contrast cradle;
// system/secure accents are compositor policy, not application ABI.
struct TrustedMarkTheme final {
    TrustedMarkRgba8 foundation {};
    TrustedMarkRgba8 system_chrome {};
    TrustedMarkRgba8 secure_system {};
};

struct TrustedMarkRasterTarget final {
    TrustedMarkRgba8* pixels {nullptr};
    std::size_t pixel_count {0U};
    std::uint32_t width {0U};
    std::uint32_t height {0U};
    std::uint32_t stride {0U};
};

struct TrustedMarkRasterStats final {
    std::uint16_t entries_seen {0U};
    std::uint16_t marks_drawn {0U};
    std::uint64_t pixel_writes {0U};
};

// Final compositor-owned CPU fallback pass for trusted attribution. It consumes
// only TrustedOverlaySnapshot, never application surface style/pixels, and draws
// after client composition. The compact asymmetric corner signature is an ENML
// baseline rather than a lock/shield icon copied from another platform.
//
// This establishes technical attribution: an application cannot cause this pass
// to run by drawing lookalike pixels. Appearance alone is not treated as a
// cryptographic proof; secure interaction still depends on compositor surface
// authority and trusted input routing.
[[nodiscard]] os::core::Result<TrustedMarkRasterStats> rasterize_trusted_marks(
    const TrustedOverlaySnapshot& overlay,
    const TrustedMarkTheme& theme,
    TrustedMarkRasterTarget target) noexcept;

} // namespace os::display
