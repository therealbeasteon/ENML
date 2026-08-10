#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/ui/raster.hpp>

namespace os::ui {

struct ContourAaStats final {
    std::uint16_t commands_seen {0U};
    std::uint16_t fringes_drawn {0U};
    std::uint64_t pixel_writes {0U};
};

// Adds a deterministic one-pixel coverage fringe around authored contours.
// This pass is deliberately separate from material translucency/backdrop work:
// it smooths geometric silhouette edges without changing ENML's semantic
// material model or requiring a GPU/vector dependency. Coverage is estimated
// with a fixed 2x2 subpixel grid and blended into caller-owned target memory.
[[nodiscard]] os::core::Result<ContourAaStats> rasterize_contour_antialias_fringe(
    const RenderCommandBuffer& commands,
    const RasterTheme& theme,
    RasterTarget target) noexcept;

} // namespace os::ui
