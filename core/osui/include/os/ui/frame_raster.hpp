#pragma once

#include <os/core/result.hpp>
#include <os/ui/contour_aa.hpp>
#include <os/ui/raster.hpp>

namespace os::ui {

struct OpaqueFrameRasterStats final {
    RasterStats materials {};
    ContourAaStats contour_antialias {};
};

// Preferred CPU/economy frame entry point for geometry. The material pass
// establishes deterministic opaque hierarchy/depth/state first, then the
// bounded contour coverage pass improves authored silhouette quality. Keeping
// the two stages explicit lets richer text/translucency work be added later
// without turning one function into an unbounded graphics framework.
[[nodiscard]] os::core::Result<OpaqueFrameRasterStats> rasterize_opaque_frame(
    const RenderCommandBuffer& commands,
    const RasterTheme& theme,
    RasterTarget target) noexcept;

} // namespace os::ui
