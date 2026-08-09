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

os::core::Result<OpaqueTextFrameRasterStats> rasterize_opaque_frame_with_text(
    const RenderCommandBuffer& commands,
    TextCommandRasterBackend text_backend,
    const RasterTheme& theme,
    RasterTarget target,
    TextCommandRasterPolicy text_policy) noexcept {
    auto geometry = rasterize_opaque_frame(commands, theme, target);
    if (!geometry) return geometry.error();

    auto text = rasterize_render_command_text(
        commands,
        text_backend,
        theme,
        target,
        text_policy);
    if (!text) return text.error();

    return OpaqueTextFrameRasterStats{
        .geometry = geometry.value(),
        .text = text.value(),
    };
}

} // namespace os::ui
