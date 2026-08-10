#include <os/ui/raster.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/ui/detail/contour_geometry.hpp>
#include <os/ui/error.hpp>

namespace {

void expect_ui_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::ui);
    assert(error.code == code);
}

constexpr std::size_t color_index(os::ui::ColorRole role) {
    return static_cast<std::size_t>(role);
}

os::ui::RasterTheme test_theme() {
    os::ui::RasterTheme theme{};
    for (auto& color : theme.colors) color = os::ui::Rgba8{0U, 0U, 0U, 255U};
    theme.colors[color_index(os::ui::ColorRole::transparent)] = {0U, 0U, 0U, 0U};
    theme.colors[color_index(os::ui::ColorRole::surface)] = {8U, 10U, 18U, 255U};
    theme.colors[color_index(os::ui::ColorRole::surface_elevated)] = {30U, 30U, 40U, 255U};
    theme.colors[color_index(os::ui::ColorRole::accent_secondary)] = {100U, 40U, 180U, 255U};
    theme.colors[color_index(os::ui::ColorRole::outline)] = {120U, 130U, 150U, 255U};
    theme.colors[color_index(os::ui::ColorRole::focus)] = {250U, 210U, 80U, 255U};
    theme.colors[color_index(os::ui::ColorRole::highlight)] = {240U, 230U, 255U, 255U};
    return theme;
}

os::ui::RenderCommandBuffer test_commands() {
    os::ui::RenderCommandBuffer buffer{};
    buffer.revision = 7U;
    buffer.count = 2U;

    auto& root = buffer.commands[0];
    root.source = os::ui::UiNodeId{1U};
    root.role = os::ui::UiRole::root;
    root.bounds = os::ui::LogicalRect{
        .x_q6 = 0,
        .y_q6 = 0,
        .width_q6 = os::ui::logical_from_dp(32U),
        .height_q6 = os::ui::logical_from_dp(24U),
    };
    root.visual.token.background = os::ui::ColorRole::surface;
    root.visual.token.material_tint = os::ui::ColorRole::transparent;
    root.visual.token.outline = os::ui::ColorRole::transparent;
    root.visual.token.material = os::ui::OpticalMaterialRole::opaque;
    root.visual.token.depth = os::ui::DepthRole::flush;
    root.visual.material.opacity_percent = 100U;
    root.contour.role = os::ui::CurveRole::rectilinear;

    auto& panel = buffer.commands[1];
    panel.source = os::ui::UiNodeId{2U};
    panel.parent = os::ui::UiNodeId{1U};
    panel.depth = 1U;
    panel.role = os::ui::UiRole::container;
    panel.bounds = os::ui::LogicalRect{
        .x_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(4U)),
        .y_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(4U)),
        .width_q6 = os::ui::logical_from_dp(24U),
        .height_q6 = os::ui::logical_from_dp(14U),
    };
    panel.visual.token.background = os::ui::ColorRole::surface_elevated;
    panel.visual.token.material_tint = os::ui::ColorRole::accent_secondary;
    panel.visual.token.outline = os::ui::ColorRole::outline;
    panel.visual.token.material = os::ui::OpticalMaterialRole::crystal;
    panel.visual.token.depth = os::ui::DepthRole::floating;
    panel.visual.material.opacity_percent = 72U;
    panel.visual.material.tint_percent = 25U;
    panel.visual.material.specular_percent = 20U;
    panel.visual.depth.offset_q6 = os::ui::logical_from_dp(2U);
    panel.visual.depth.opacity_percent = 20U;
    panel.contour.role = os::ui::CurveRole::swept;
    panel.contour.radii = os::ui::CornerRadii{
        .top_left_q6 = os::ui::logical_from_dp(2U),
        .top_right_q6 = os::ui::logical_from_dp(4U),
        .bottom_right_q6 = os::ui::logical_from_dp(3U),
        .bottom_left_q6 = os::ui::logical_from_dp(5U),
    };
    panel.contour.smoothing_percent = 85U;
    panel.contour.asymmetric = true;
    panel.focus_visible = true;

    return buffer;
}

} // namespace

int main() {
    constexpr std::uint32_t width = 32U;
    constexpr std::uint32_t height = 24U;
    std::array<os::ui::Rgba8, width * height> pixels{};

    auto theme = test_theme();
    auto commands = test_commands();
    const os::ui::RasterTarget target{
        .pixels = pixels.data(),
        .pixel_count = pixels.size(),
        .width = width,
        .height = height,
        .stride = width,
        .scale = os::ui::RasterScale{1U, os::ui::logical_units_per_dp},
    };

    auto raster = os::ui::rasterize_opaque_materials(commands, theme, target);
    assert(raster);
    assert(raster.value().commands_seen == 2U);
    assert(raster.value().surfaces_filled == 2U);
    assert(raster.value().shadows_drawn == 1U);
    assert(raster.value().lit_edges_drawn == 1U);
    assert(raster.value().shaded_edges_drawn == 1U);
    assert(raster.value().partial_coverage_writes > 0U);
    assert(raster.value().pixel_writes >= width * height);

    const auto surface = theme.colors[color_index(os::ui::ColorRole::surface)];
    const auto focus = theme.colors[color_index(os::ui::ColorRole::focus)];
    const os::ui::Rgba8 panel_fill{48U, 33U, 75U, 255U};
    const os::ui::Rgba8 shadowed_surface{6U, 8U, 14U, 255U};
    const os::ui::Rgba8 partial_shadowed_surface{7U, 9U, 15U, 255U};

    // Root fill proves an actual pixel target is written.
    assert(pixels[0] == surface);

    // The swept panel does not collapse to a generic rectangle: the authored
    // top-right contour clips its extreme center-sampled corner while leaving
    // the center fully filled.
    assert(pixels[4U * width + 27U] == surface);
    assert(pixels[10U * width + 16U] == panel_fill);

    // The high-smoothing swept edge now has real *interior* subpixel coverage.
    // This pixel's center is inside the contour, but only three of four fixed
    // subpixel samples are inside. Focus therefore remains visible while the
    // edge is neither a binary root pixel nor an unnaturally full focus pixel.
    const auto interior_edge = pixels[4U * width + 26U];
    assert(interior_edge.alpha == 255U);
    assert(interior_edge != surface);
    assert(interior_edge != focus);

    // Depth has a useful opaque fallback before blur/alpha exists: a fully
    // covered part of the floating panel shadow darkens support by the entire
    // requested 20%.
    assert(pixels[18U * width + 28U] == shadowed_surface);

    // The top-right shadow silhouette is coverage-aware too. At (28,6), three
    // of four fixed samples are inside the offset swept contour, so the 20%
    // shadow is attenuated to 15% rather than producing a jagged binary step.
    // This pixel lies outside the panel itself and therefore isolates shadow AA.
    assert(pixels[6U * width + 28U] == partial_shadowed_surface);

    // Focus is rendered as an explicit edge treatment rather than encoded only
    // by material/transparency, keeping interaction state legible after both
    // leading highlight and trailing occlusion have been applied.
    assert(pixels[4U * width + 5U] == focus);

    // Inset material reverses the directional edge model and does not cast an
    // external positive-offset shadow. Its leading edge darkens into the
    // surface while the trailing edge catches the highlight, producing a
    // recessed reading with no blur surface or shader.
    std::array<os::ui::Rgba8, width * height> inset_pixels{};
    auto inset_commands = test_commands();
    auto& inset = inset_commands.commands[1];
    inset.bounds = os::ui::LogicalRect{
        .x_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(8U)),
        .y_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(6U)),
        .width_q6 = os::ui::logical_from_dp(12U),
        .height_q6 = os::ui::logical_from_dp(8U),
    };
    inset.visual.token.background = os::ui::ColorRole::surface_elevated;
    inset.visual.token.material_tint = os::ui::ColorRole::transparent;
    inset.visual.token.outline = os::ui::ColorRole::transparent;
    inset.visual.token.material = os::ui::OpticalMaterialRole::opaque;
    inset.visual.token.depth = os::ui::DepthRole::inset;
    inset.visual.material.tint_percent = 0U;
    inset.visual.material.specular_percent = 20U;
    inset.visual.depth.offset_q6 = os::ui::logical_from_dp(1U);
    inset.visual.depth.opacity_percent = 30U;
    inset.contour.role = os::ui::CurveRole::rectilinear;
    inset.contour.radii = {};
    inset.contour.smoothing_percent = 0U;
    inset.focus_visible = false;
    const os::ui::RasterTarget inset_target{
        .pixels = inset_pixels.data(),
        .pixel_count = inset_pixels.size(),
        .width = width,
        .height = height,
        .stride = width,
        .scale = os::ui::RasterScale{1U, os::ui::logical_units_per_dp},
    };
    auto inset_raster = os::ui::rasterize_opaque_materials(
        inset_commands, theme, inset_target);
    assert(inset_raster);
    assert(inset_raster.value().shadows_drawn == 0U);
    assert(inset_raster.value().lit_edges_drawn == 1U);
    assert(inset_raster.value().shaded_edges_drawn == 1U);
    const os::ui::Rgba8 inset_leading{27U, 27U, 36U, 255U};
    const os::ui::Rgba8 inset_trailing{72U, 70U, 83U, 255U};
    assert(inset_pixels[6U * width + 8U] == inset_leading);
    assert(inset_pixels[13U * width + 19U] == inset_trailing);
    assert(inset_pixels[14U * width + 20U] == surface);

    // With smoothing removed, the same top-right sample's center falls outside
    // the circular corner and therefore remains the root surface in the primary
    // raster. The separate perimeter AA stage may later add outside coverage.
    std::array<os::ui::Rgba8, width * height> circular_pixels{};
    auto circular_commands = commands;
    circular_commands.commands[1].contour.smoothing_percent = 0U;
    const os::ui::RasterTarget circular_target{
        .pixels = circular_pixels.data(),
        .pixel_count = circular_pixels.size(),
        .width = width,
        .height = height,
        .stride = width,
        .scale = os::ui::RasterScale{1U, os::ui::logical_units_per_dp},
    };
    auto circular_raster = os::ui::rasterize_opaque_materials(
        circular_commands, theme, circular_target);
    assert(circular_raster);
    assert(circular_pixels[4U * width + 26U] == surface);

    // The shared normalized evaluator must remain well-defined at the maximum
    // valid logical contour and the highest supported raster numerator. The
    // old direct fourth-power formulation could wrap uint64_t here. The outer
    // corner is empty while the adjacent body sample remains fully covered.
    os::ui::RenderCommand extreme{};
    extreme.source = os::ui::UiNodeId{3U};
    extreme.bounds = os::ui::LogicalRect{
        .x_q6 = 0,
        .y_q6 = 0,
        .width_q6 = os::ui::max_logical_dimension_q6,
        .height_q6 = os::ui::max_logical_dimension_q6,
    };
    const std::uint32_t maximum_radius_q6 = os::ui::max_logical_dimension_q6 / 2U;
    extreme.contour.radii = os::ui::CornerRadii{
        .top_left_q6 = maximum_radius_q6,
        .top_right_q6 = maximum_radius_q6,
        .bottom_right_q6 = maximum_radius_q6,
        .bottom_left_q6 = maximum_radius_q6,
    };
    extreme.contour.smoothing_percent = 100U;
    const auto extreme_contour = os::ui::raster_detail::pixel_contour(
        extreme,
        os::ui::RasterScale{16U, 1U});
    assert(os::ui::raster_detail::coverage_2x2(extreme_contour, 0, 0) == 0U);
    assert(os::ui::raster_detail::coverage_2x2(
        extreme_contour,
        extreme_contour.top_left_radius,
        0) == 255U);

    auto bad_target = target;
    bad_target.pixel_count = 1U;
    auto invalid_target = os::ui::rasterize_opaque_materials(commands, theme, bad_target);
    assert(!invalid_target);
    expect_ui_error(invalid_target.error(), os::ui::errors::invalid_raster_target);

    auto bad_theme = theme;
    bad_theme.colors[color_index(os::ui::ColorRole::transparent)].alpha = 255U;
    auto invalid_theme = os::ui::rasterize_opaque_materials(commands, bad_theme, target);
    assert(!invalid_theme);
    expect_ui_error(invalid_theme.error(), os::ui::errors::invalid_raster_theme);

    auto bad_commands = commands;
    bad_commands.commands[1].source = {};
    auto invalid_command = os::ui::rasterize_opaque_materials(bad_commands, theme, target);
    assert(!invalid_command);
    expect_ui_error(invalid_command.error(), os::ui::errors::invalid_raster_command);

    auto bad_smoothing = commands;
    bad_smoothing.commands[1].contour.smoothing_percent = 101U;
    auto invalid_smoothing = os::ui::rasterize_opaque_materials(
        bad_smoothing, theme, target);
    assert(!invalid_smoothing);
    expect_ui_error(invalid_smoothing.error(), os::ui::errors::invalid_raster_command);

    return 0;
}
