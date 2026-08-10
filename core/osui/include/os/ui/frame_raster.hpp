#pragma once

#include <os/core/result.hpp>
#include <os/ui/contour_aa.hpp>
#include <os/ui/raster.hpp>
#include <os/ui/text_command_raster.hpp>

namespace os::ui {

struct OpaqueFrameRasterStats final {
    RasterStats materials {};
    ContourAaStats contour_antialias {};
};

struct OpaqueTextFrameRasterStats final {
    OpaqueFrameRasterStats geometry {};
    TextCommandRasterStats text {};
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

// Preferred CPU/economy path once renderer-private font backends are present.
// Geometry is painted first and visible text is then shaped and rasterized
// directly from the same RenderCommandBuffer. There is no secondary app-owned
// text display list and no hidden background font/render worker.
[[nodiscard]] os::core::Result<OpaqueTextFrameRasterStats> rasterize_opaque_frame_with_text(
    const RenderCommandBuffer& commands,
    TextCommandRasterBackend text_backend,
    const RasterTheme& theme,
    RasterTarget target,
    TextCommandRasterPolicy text_policy = {}) noexcept;

} // namespace os::ui
