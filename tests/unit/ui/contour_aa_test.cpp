#include <os/ui/contour_aa.hpp>
#include <os/ui/frame_raster.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>
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
    theme.colors[color_index(os::ui::ColorRole::surface)] = {10U, 12U, 20U, 255U};
    theme.colors[color_index(os::ui::ColorRole::surface_elevated)] = {36U, 34U, 48U, 255U};
    theme.colors[color_index(os::ui::ColorRole::accent_secondary)] = {118U, 52U, 184U, 255U};
    theme.colors[color_index(os::ui::ColorRole::outline)] = {142U, 150U, 170U, 255U};
    theme.colors[color_index(os::ui::ColorRole::focus)] = {248U, 214U, 92U, 255U};
    theme.colors[color_index(os::ui::ColorRole::highlight)] = {244U, 236U, 255U, 255U};
    return theme;
}

os::ui::RenderCommandBuffer commands() {
    os::ui::RenderCommandBuffer buffer{};
    buffer.revision = 12U;
    buffer.count = 2U;

    auto& root = buffer.commands[0];
    root.source = os::ui::UiNodeId{1U};
    root.role = os::ui::UiRole::root;
    root.bounds = {
        0,
        0,
        os::ui::logical_from_dp(32U),
        os::ui::logical_from_dp(24U),
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
    panel.parent = root.source;
    panel.depth = 1U;
    panel.role = os::ui::UiRole::container;
    panel.bounds = {
        static_cast<std::int32_t>(os::ui::logical_from_dp(4U)),
        static_cast<std::int32_t>(os::ui::logical_from_dp(4U)),
        os::ui::logical_from_dp(24U),
        os::ui::logical_from_dp(14U),
    };
    panel.visual.token.background = os::ui::ColorRole::surface_elevated;
    panel.visual.token.material_tint = os::ui::ColorRole::accent_secondary;
    panel.visual.token.outline = os::ui::ColorRole::outline;
    panel.visual.token.material = os::ui::OpticalMaterialRole::crystal;
    panel.visual.token.depth = os::ui::DepthRole::floating;
    panel.visual.material.opacity_percent = 72U;
    panel.visual.material.tint_percent = 25U;
    panel.contour.role = os::ui::CurveRole::swept;
    panel.contour.radii = {
        os::ui::logical_from_dp(2U),
        os::ui::logical_from_dp(4U),
        os::ui::logical_from_dp(3U),
        os::ui::logical_from_dp(5U),
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

    const auto theme = test_theme();
    auto buffer = commands();
    const os::ui::RasterTarget target{
        .pixels = pixels.data(),
        .pixel_count = pixels.size(),
        .width = width,
        .height = height,
        .stride = width,
        .scale = {1U, os::ui::logical_units_per_dp},
    };

    auto opaque = os::ui::rasterize_opaque_materials(buffer, theme, target);
    assert(opaque);
    const auto before = pixels;

    auto aa = os::ui::rasterize_contour_antialias_fringe(buffer, theme, target);
    assert(aa);
    assert(aa.value().commands_seen == 2U);
    assert(aa.value().fringes_drawn == 1U);
    assert(aa.value().pixel_writes > 0U);

    std::size_t changed = 0U;
    for (std::size_t index = 0U; index < pixels.size(); ++index) {
        if (pixels[index] == before[index]) continue;
        ++changed;
        // This test scene has an opaque root, so partial contour coverage must
        // preserve an opaque final framebuffer pixel rather than punching a
        // transparent halo through the supporting surface.
        assert(pixels[index].alpha == 255U);
    }
    assert(changed == aa.value().pixel_writes);

    // The binary material raster left this extreme swept corner as root
    // surface. Fixed-grid coverage extends a partial focus-colored fringe into
    // it without turning it into a fully covered focus pixel.
    const std::size_t sample = 4U * width + 27U;
    assert(before[sample] == theme.colors[color_index(os::ui::ColorRole::surface)]);
    assert(pixels[sample] != before[sample]);
    assert(pixels[sample] != theme.colors[color_index(os::ui::ColorRole::focus)]);

    // The preferred composed CPU frame entry point must produce exactly the
    // same deterministic material + contour result as invoking both stages in
    // order by hand.
    std::array<os::ui::Rgba8, width * height> frame_pixels{};
    const os::ui::RasterTarget frame_target{
        .pixels = frame_pixels.data(),
        .pixel_count = frame_pixels.size(),
        .width = width,
        .height = height,
        .stride = width,
        .scale = {1U, os::ui::logical_units_per_dp},
    };
    auto frame = os::ui::rasterize_opaque_frame(buffer, theme, frame_target);
    assert(frame);
    assert(frame.value().materials.commands_seen == 2U);
    assert(frame.value().contour_antialias.fringes_drawn == 1U);
    assert(frame_pixels == pixels);

    // A rectilinear-only scene has no curved partial coverage to add.
    std::array<os::ui::Rgba8, width * height> flat_pixels{};
    auto flat_buffer = buffer;
    flat_buffer.count = 1U;
    const os::ui::RasterTarget flat_target{
        .pixels = flat_pixels.data(),
        .pixel_count = flat_pixels.size(),
        .width = width,
        .height = height,
        .stride = width,
        .scale = {1U, os::ui::logical_units_per_dp},
    };
    assert(os::ui::rasterize_opaque_materials(flat_buffer, theme, flat_target));
    auto flat_aa = os::ui::rasterize_contour_antialias_fringe(flat_buffer, theme, flat_target);
    assert(flat_aa);
    assert(flat_aa.value().fringes_drawn == 0U);
    assert(flat_aa.value().pixel_writes == 0U);

    auto bad_target = target;
    bad_target.pixel_count = 1U;
    auto invalid_target =
        os::ui::rasterize_contour_antialias_fringe(buffer, theme, bad_target);
    assert(!invalid_target);
    expect_ui_error(invalid_target.error(), os::ui::errors::invalid_raster_target);

    auto bad_theme = theme;
    bad_theme.colors[color_index(os::ui::ColorRole::transparent)].alpha = 255U;
    auto invalid_theme =
        os::ui::rasterize_contour_antialias_fringe(buffer, bad_theme, target);
    assert(!invalid_theme);
    expect_ui_error(invalid_theme.error(), os::ui::errors::invalid_raster_theme);

    auto bad_commands = buffer;
    bad_commands.commands[1].contour.smoothing_percent = 101U;
    auto invalid_command =
        os::ui::rasterize_contour_antialias_fringe(bad_commands, theme, target);
    assert(!invalid_command);
    expect_ui_error(invalid_command.error(), os::ui::errors::invalid_raster_command);

    return 0;
}
