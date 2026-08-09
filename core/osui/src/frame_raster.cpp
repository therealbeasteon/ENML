#include <os/ui/frame_raster.hpp>

namespace os::ui {

os::core::Result<OpaqueFrameRasterStats> rasterize_opaque_frame(
    const RenderCommandBuffer& commands,
    const RasterTheme& theme,
    RasterTarget target) noexcept {
    auto materials = rasterize_opaque_materials(commands, theme, target);
    if (!materials) return materials.error();

    auto contour_antialias = rasterize_contour_antialias_fringe(commands, theme, target);
    if (!contour_antialias) return contour_antialias.error();

    return OpaqueFrameRasterStats{
        .materials = materials.value(),
        .contour_antialias = contour_antialias.value(),
    };
}

} // namespace os::ui
