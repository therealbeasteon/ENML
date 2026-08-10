#include <os/ui/raster.hpp>

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

[[nodiscard]] constexpr bool depth_role_valid(DepthRole role) noexcept {
    switch (role) {
    case DepthRole::flush:
    case DepthRole::inset:
    case DepthRole::raised:
    case DepthRole::floating:
    case DepthRole::hero:
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

[[nodiscard]] constexpr Rgba8 darken_opaque(
    Rgba8 base,
    std::uint8_t percent) noexcept {
    return Rgba8{
        .red = blend_channel(base.red, 0U, percent),
        .green = blend_channel(base.green, 0U, percent),
        .blue = blend_channel(base.blue, 0U, percent),
        .alpha = 255U,
    };
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
        depth_role_valid(command.visual.token.depth) &&
        curve_role_valid(command.contour.role) &&
        command.visual.material.tint_percent <= 100U &&
        command.visual.material.opacity_percent <= 100U &&
        command.visual.material.specular_percent <= 100U &&
        command.visual.depth.opacity_percent <= 100U &&
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

[[nodiscard]] Rgba8 resolved_fill(
    const RenderCommand& command,
    const RasterTheme& theme) noexcept {
    Rgba8 base = theme.colors[color_index(command.visual.token.background)];
    if (base.alpha == 0U) return base;

    if (command.visual.material.tint_percent != 0U &&
        command.visual.token.material_tint != ColorRole::transparent) {
        const Rgba8 tint = theme.colors[color_index(command.visual.token.material_tint)];
        base = blend_opaque(base, tint, command.visual.material.tint_percent);
    }
    base.alpha = 255U;
    return base;
}

[[nodiscard]] std::size_t pixel_index(
    const RasterTarget& target,
    std::uint32_t x,
    std::uint32_t y) noexcept {
    return static_cast<std::size_t>(y) * target.stride + static_cast<std::size_t>(x);
}

void write_pixel(
    RasterTarget target,
    std::uint32_t x,
    std::uint32_t y,
    Rgba8 color,
    RasterStats& stats) noexcept {
    target.pixels[pixel_index(target, x, y)] = color;
    ++stats.pixel_writes;
}

void write_pixel_coverage(
    RasterTarget target,
    std::uint32_t x,
    std::uint32_t y,
    Rgba8 color,
    std::uint8_t coverage,
    RasterStats& stats) noexcept {
    if (coverage == 0U) return;
    if (coverage == 255U) {
        write_pixel(target, x, y, color, stats);
        return;
    }
    const std::size_t index = pixel_index(target, x, y);
    target.pixels[index] = blend_coverage(target.pixels[index], color, coverage);
    ++stats.partial_coverage_writes;
    ++stats.pixel_writes;
}

[[nodiscard]] bool darken_existing_pixel_coverage(
    RasterTarget target,
    std::uint32_t x,
    std::uint32_t y,
    std::uint8_t percent,
    std::uint8_t coverage,
    RasterStats& stats) noexcept {
    if (coverage == 0U) return false;
    const std::size_t index = pixel_index(target, x, y);
    const Rgba8 existing = target.pixels[index];
    if (existing.alpha == 0U) return false;

    const std::uint8_t effective_percent = static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(percent) * coverage + 127U) / 255U);
    if (effective_percent == 0U) return false;

    target.pixels[index] = darken_opaque(existing, effective_percent);
    target.pixels[index].alpha = existing.alpha;
    if (coverage != 255U) ++stats.partial_coverage_writes;
    ++stats.pixel_writes;
    return true;
}

} // namespace

os::core::Result<RasterStats> rasterize_opaque_materials(
    const RenderCommandBuffer& commands,
    const RasterTheme& theme,
    RasterTarget target) noexcept {
    if (!target_valid(target)) return ui_error(errors::invalid_raster_target);
    if (!theme_valid(theme)) return ui_error(errors::invalid_raster_theme);
    if (commands.count > commands.commands.size()) {
        return ui_error(errors::invalid_raster_command);
    }

    RasterStats stats {};
    for (std::size_t command_index = 0U; command_index < commands.count; ++command_index) {
        const RenderCommand& command = commands.commands[command_index];
        ++stats.commands_seen;
        if (!command_valid(command)) return ui_error(errors::invalid_raster_command);

        const raster_detail::PixelContour contour =
            raster_detail::pixel_contour(command, target.scale);
        const Rgba8 fill = resolved_fill(command, theme);

        // Opaque depth fallback: before blur kernels or alpha shadows exist,
        // raised material darkens already-painted supporting pixels at a
        // deterministic positive offset. Material, depth and fringe stages all
        // use the same renderer-private contour evaluator so authored geometry
        // cannot drift between passes.
        const std::int64_t shadow_offset = raster_detail::scale_radius(
            command.visual.depth.offset_q6,
            target.scale);
        if (fill.alpha != 0U &&
            command.visual.token.material != OpticalMaterialRole::none &&
            command.visual.token.depth != DepthRole::flush &&
            command.visual.depth.opacity_percent != 0U &&
            shadow_offset > 0) {
            const raster_detail::PixelContour shadow_contour =
                raster_detail::offset_contour(contour, shadow_offset);
            const std::int64_t shadow_left =
                std::max<std::int64_t>(0, shadow_contour.left - 1);
            const std::int64_t shadow_top =
                std::max<std::int64_t>(0, shadow_contour.top - 1);
            const std::int64_t shadow_right =
                std::min<std::int64_t>(target.width, shadow_contour.right + 1);
            const std::int64_t shadow_bottom =
                std::min<std::int64_t>(target.height, shadow_contour.bottom + 1);
            bool shadow_written = false;
            for (std::int64_t y = shadow_top; y < shadow_bottom; ++y) {
                for (std::int64_t x = shadow_left; x < shadow_right; ++x) {
                    const std::uint8_t coverage =
                        raster_detail::coverage_2x2(shadow_contour, x, y);
                    if (coverage == 0U) continue;
                    shadow_written = darken_existing_pixel_coverage(
                        target,
                        static_cast<std::uint32_t>(x),
                        static_cast<std::uint32_t>(y),
                        command.visual.depth.opacity_percent,
                        coverage,
                        stats) || shadow_written;
                }
            }
            if (shadow_written) ++stats.shadows_drawn;
        }

        const std::int64_t clipped_left = std::max<std::int64_t>(0, contour.left);
        const std::int64_t clipped_top = std::max<std::int64_t>(0, contour.top);
        const std::int64_t clipped_right =
            std::min<std::int64_t>(target.width, contour.right);
        const std::int64_t clipped_bottom =
            std::min<std::int64_t>(target.height, contour.bottom);
        if (clipped_left >= clipped_right || clipped_top >= clipped_bottom) continue;

        bool filled = false;
        if (fill.alpha != 0U) {
            for (std::int64_t y = clipped_top; y < clipped_bottom; ++y) {
                for (std::int64_t x = clipped_left; x < clipped_right; ++x) {
                    if (!raster_detail::contains_center(contour, x, y)) continue;
                    const std::uint8_t coverage = raster_detail::coverage_2x2(contour, x, y);
                    write_pixel_coverage(
                        target,
                        static_cast<std::uint32_t>(x),
                        static_cast<std::uint32_t>(y),
                        fill,
                        coverage,
                        stats);
                    filled = coverage != 0U || filled;
                }
            }
        }
        if (filled) ++stats.surfaces_filled;

        // A bounded leading-edge highlight gives non-flush material an optical
        // light direction even when live translucency/specular shaders are not
        // available. Focus/outline is painted after this and therefore always
        // remains the final, unambiguous state cue.
        if (fill.alpha != 0U &&
            command.visual.token.material != OpticalMaterialRole::none &&
            command.visual.token.depth != DepthRole::flush &&
            command.visual.material.specular_percent != 0U) {
            const Rgba8 highlight = theme.colors[color_index(ColorRole::highlight)];
            bool lit = false;
            for (std::int64_t y = clipped_top; y < clipped_bottom; ++y) {
                for (std::int64_t x = clipped_left; x < clipped_right; ++x) {
                    if (!raster_detail::leading_boundary_center(contour, x, y)) continue;
                    const std::uint32_t ux = static_cast<std::uint32_t>(x);
                    const std::uint32_t uy = static_cast<std::uint32_t>(y);
                    const std::size_t index = pixel_index(target, ux, uy);
                    const std::uint8_t alpha = target.pixels[index].alpha;
                    target.pixels[index] = blend_opaque(
                        target.pixels[index],
                        highlight,
                        command.visual.material.specular_percent);
                    target.pixels[index].alpha = alpha;
                    ++stats.pixel_writes;
                    lit = true;
                }
            }
            if (lit) ++stats.lit_edges_drawn;
        }

        const ColorRole outline_role =
            command.focus_visible ? ColorRole::focus : command.visual.token.outline;
        if (outline_role == ColorRole::transparent) continue;
        const Rgba8 outline = theme.colors[color_index(outline_role)];
        for (std::int64_t y = clipped_top; y < clipped_bottom; ++y) {
            for (std::int64_t x = clipped_left; x < clipped_right; ++x) {
                if (!raster_detail::boundary_center(contour, x, y)) continue;
                const std::uint8_t coverage = raster_detail::coverage_2x2(contour, x, y);
                write_pixel_coverage(
                    target,
                    static_cast<std::uint32_t>(x),
                    static_cast<std::uint32_t>(y),
                    outline,
                    coverage,
                    stats);
            }
        }
    }
    return stats;
}

} // namespace os::ui
