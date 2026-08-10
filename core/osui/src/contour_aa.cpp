#include <os/ui/contour_aa.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <os/ui/detail/contour_geometry.hpp>
#include <os/ui/error.hpp>

namespace os::ui {
namespace {

[[nodiscard]] constexpr bool color_role_valid(ColorRole role) noexcept {
    switch (role) {
    case ColorRole::transparent:
    case ColorRole::surface:
    case ColorRole::surface_elevated:
    case ColorRole::text_primary:
    case ColorRole::text_secondary:
    case ColorRole::accent:
    case ColorRole::on_accent:
    case ColorRole::outline:
    case ColorRole::focus:
    case ColorRole::critical:
    case ColorRole::on_critical:
    case ColorRole::accent_secondary:
    case ColorRole::accent_tertiary:
    case ColorRole::highlight:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool curve_role_valid(CurveRole role) noexcept {
    switch (role) {
    case CurveRole::rectilinear:
    case CurveRole::soft:
    case CurveRole::continuous:
    case CurveRole::swept:
    case CurveRole::capsule:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr std::size_t color_index(ColorRole role) noexcept {
    return static_cast<std::size_t>(role);
}

[[nodiscard]] bool theme_valid(const RasterTheme& theme) noexcept {
    if (theme.colors[color_index(ColorRole::transparent)].alpha != 0U) return false;
    for (std::size_t index = 1U; index < theme.colors.size(); ++index) {
        if (theme.colors[index].alpha != 255U) return false;
    }
    return true;
}

[[nodiscard]] bool target_valid(const RasterTarget& target) noexcept {
    if (target.pixels == nullptr || target.width == 0U || target.height == 0U ||
        target.width > max_raster_dimension || target.height > max_raster_dimension ||
        target.stride < target.width || target.stride > max_raster_dimension ||
        target.scale.numerator == 0U || target.scale.numerator > 16U ||
        target.scale.denominator == 0U || target.scale.denominator > 1024U) {
        return false;
    }
    const std::size_t required =
        static_cast<std::size_t>(target.stride) * static_cast<std::size_t>(target.height);
    return target.pixel_count >= required;
}

[[nodiscard]] constexpr std::uint8_t blend_channel(
    std::uint8_t base,
    std::uint8_t tint,
    std::uint8_t percent) noexcept {
    const std::uint32_t inverse = 100U - percent;
    return static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(base) * inverse +
         static_cast<std::uint32_t>(tint) * percent + 50U) /
        100U);
}

[[nodiscard]] constexpr Rgba8 blend_opaque(
    Rgba8 base,
    Rgba8 tint,
    std::uint8_t tint_percent) noexcept {
    return Rgba8{
        .red = blend_channel(base.red, tint.red, tint_percent),
        .green = blend_channel(base.green, tint.green, tint_percent),
        .blue = blend_channel(base.blue, tint.blue, tint_percent),
        .alpha = 255U,
    };
}

[[nodiscard]] Rgba8 resolved_fill(
    const RenderCommand& command,
    const RasterTheme& theme) noexcept {
    Rgba8 base = theme.colors[color_index(command.visual.token.background)];
    if (base.alpha == 0U) return base;
    if (command.visual.material.tint_percent != 0U &&
        command.visual.token.material_tint != ColorRole::transparent) {
        base = blend_opaque(
            base,
            theme.colors[color_index(command.visual.token.material_tint)],
            command.visual.material.tint_percent);
    }
    return base;
}

[[nodiscard]] Rgba8 edge_color(
    const RenderCommand& command,
    const RasterTheme& theme) noexcept {
    if (command.focus_visible) return theme.colors[color_index(ColorRole::focus)];
    if (command.visual.token.outline != ColorRole::transparent) {
        return theme.colors[color_index(command.visual.token.outline)];
    }
    return resolved_fill(command, theme);
}

[[nodiscard]] constexpr std::uint8_t blend_channel_coverage(
    std::uint8_t destination,
    std::uint8_t source,
    std::uint8_t coverage) noexcept {
    const std::uint32_t inverse = 255U - coverage;
    return static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(destination) * inverse +
         static_cast<std::uint32_t>(source) * coverage + 127U) /
        255U);
}

[[nodiscard]] constexpr Rgba8 blend_coverage(
    Rgba8 destination,
    Rgba8 source,
    std::uint8_t coverage) noexcept {
    return Rgba8{
        .red = blend_channel_coverage(destination.red, source.red, coverage),
        .green = blend_channel_coverage(destination.green, source.green, coverage),
        .blue = blend_channel_coverage(destination.blue, source.blue, coverage),
        .alpha = static_cast<std::uint8_t>(
            coverage +
            (static_cast<std::uint32_t>(destination.alpha) * (255U - coverage) + 127U) /
                255U),
    };
}

[[nodiscard]] bool command_valid(const RenderCommand& command) noexcept {
    return command.source.value() != 0U && command.bounds.bounded() &&
        color_role_valid(command.visual.token.background) &&
        color_role_valid(command.visual.token.material_tint) &&
        color_role_valid(command.visual.token.outline) &&
        curve_role_valid(command.contour.role) &&
        command.visual.material.tint_percent <= 100U &&
        command.contour.smoothing_percent <= 100U &&
        command.contour.radii.top_left_q6 <= command.bounds.width_q6 / 2U &&
        command.contour.radii.top_left_q6 <= command.bounds.height_q6 / 2U &&
        command.contour.radii.top_right_q6 <= command.bounds.width_q6 / 2U &&
        command.contour.radii.top_right_q6 <= command.bounds.height_q6 / 2U &&
        command.contour.radii.bottom_right_q6 <= command.bounds.width_q6 / 2U &&
        command.contour.radii.bottom_right_q6 <= command.bounds.height_q6 / 2U &&
        command.contour.radii.bottom_left_q6 <= command.bounds.width_q6 / 2U &&
        command.contour.radii.bottom_left_q6 <= command.bounds.height_q6 / 2U;
}

[[nodiscard]] std::size_t pixel_index(
    const RasterTarget& target,
    std::uint32_t x,
    std::uint32_t y) noexcept {
    return static_cast<std::size_t>(y) * target.stride + static_cast<std::size_t>(x);
}

} // namespace

os::core::Result<ContourAaStats> rasterize_contour_antialias_fringe(
    const RenderCommandBuffer& commands,
    const RasterTheme& theme,
    RasterTarget target) noexcept {
    if (!target_valid(target)) return ui_error(errors::invalid_raster_target);
    if (!theme_valid(theme)) return ui_error(errors::invalid_raster_theme);
    if (commands.count > commands.commands.size()) {
        return ui_error(errors::invalid_raster_command);
    }

    ContourAaStats stats {};
    for (std::size_t command_index = 0U; command_index < commands.count; ++command_index) {
        const RenderCommand& command = commands.commands[command_index];
        ++stats.commands_seen;
        if (!command_valid(command)) return ui_error(errors::invalid_raster_command);

        const raster_detail::PixelContour contour =
            raster_detail::pixel_contour(command, target.scale);
        const Rgba8 source = edge_color(command, theme);
        if (source.alpha == 0U || !raster_detail::has_curved_corners(contour)) {
            continue;
        }

        const std::int64_t scan_left = std::max<std::int64_t>(0, contour.left - 1);
        const std::int64_t scan_top = std::max<std::int64_t>(0, contour.top - 1);
        const std::int64_t scan_right =
            std::min<std::int64_t>(target.width, contour.right + 1);
        const std::int64_t scan_bottom =
            std::min<std::int64_t>(target.height, contour.bottom + 1);

        bool wrote_fringe = false;
        for (std::int64_t y = scan_top; y < scan_bottom; ++y) {
            for (std::int64_t x = scan_left; x < scan_right; ++x) {
                // The primary material raster owns pixels whose center is
                // inside the contour. This complementary pass adds only the
                // partial outside coverage from the exact same shared contour
                // evaluator, preventing edge seams caused by geometry drift.
                if (raster_detail::contains_center(contour, x, y)) continue;
                const std::uint8_t coverage = raster_detail::coverage_2x2(contour, x, y);
                if (coverage == 0U) continue;

                const auto ux = static_cast<std::uint32_t>(x);
                const auto uy = static_cast<std::uint32_t>(y);
                const std::size_t index = pixel_index(target, ux, uy);
                target.pixels[index] = blend_coverage(target.pixels[index], source, coverage);
                ++stats.pixel_writes;
                wrote_fringe = true;
            }
        }
        if (wrote_fringe) ++stats.fringes_drawn;
    }
    return stats;
}

} // namespace os::ui
