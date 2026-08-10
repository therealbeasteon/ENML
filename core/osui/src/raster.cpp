#include <os/ui/raster.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <os/ui/error.hpp>

namespace os::ui {
namespace {

struct PixelRect final {
    std::int64_t left {0};
    std::int64_t top {0};
    std::int64_t right {0};
    std::int64_t bottom {0};
    std::int64_t top_left_radius {0};
    std::int64_t top_right_radius {0};
    std::int64_t bottom_right_radius {0};
    std::int64_t bottom_left_radius {0};
    std::uint8_t smoothing_percent {0U};
};

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

[[nodiscard]] constexpr std::int64_t floor_div(
    std::int64_t value,
    std::int64_t divisor) noexcept {
    if (value >= 0) return value / divisor;
    return -(((-value) + divisor - 1) / divisor);
}

[[nodiscard]] constexpr std::int64_t ceil_div(
    std::int64_t value,
    std::int64_t divisor) noexcept {
    if (value >= 0) return (value + divisor - 1) / divisor;
    return -((-value) / divisor);
}

[[nodiscard]] std::int64_t scale_floor(
    std::int64_t q6,
    const RasterScale& scale) noexcept {
    return floor_div(
        q6 * static_cast<std::int64_t>(scale.numerator),
        static_cast<std::int64_t>(scale.denominator));
}

[[nodiscard]] std::int64_t scale_ceil(
    std::int64_t q6,
    const RasterScale& scale) noexcept {
    return ceil_div(
        q6 * static_cast<std::int64_t>(scale.numerator),
        static_cast<std::int64_t>(scale.denominator));
}

[[nodiscard]] std::int64_t scale_radius(
    std::uint32_t q6,
    const RasterScale& scale) noexcept {
    if (q6 == 0U) return 0;
    const std::uint64_t numerator =
        static_cast<std::uint64_t>(q6) * scale.numerator;
    return static_cast<std::int64_t>(
        (numerator + scale.denominator - 1U) / scale.denominator);
}

[[nodiscard]] PixelRect pixel_rect(
    const RenderCommand& command,
    const RasterScale& scale) noexcept {
    const std::int64_t left_q6 = command.bounds.x_q6;
    const std::int64_t top_q6 = command.bounds.y_q6;
    const std::int64_t right_q6 =
        left_q6 + static_cast<std::int64_t>(command.bounds.width_q6);
    const std::int64_t bottom_q6 =
        top_q6 + static_cast<std::int64_t>(command.bounds.height_q6);

    PixelRect rect {
        .left = scale_floor(left_q6, scale),
        .top = scale_floor(top_q6, scale),
        .right = scale_ceil(right_q6, scale),
        .bottom = scale_ceil(bottom_q6, scale),
        .top_left_radius = scale_radius(command.contour.radii.top_left_q6, scale),
        .top_right_radius = scale_radius(command.contour.radii.top_right_q6, scale),
        .bottom_right_radius = scale_radius(command.contour.radii.bottom_right_q6, scale),
        .bottom_left_radius = scale_radius(command.contour.radii.bottom_left_q6, scale),
        .smoothing_percent = command.contour.smoothing_percent,
    };

    const std::int64_t half_width = std::max<std::int64_t>(0, (rect.right - rect.left) / 2);
    const std::int64_t half_height = std::max<std::int64_t>(0, (rect.bottom - rect.top) / 2);
    const std::int64_t maximum_radius = std::min(half_width, half_height);
    rect.top_left_radius = std::min(rect.top_left_radius, maximum_radius);
    rect.top_right_radius = std::min(rect.top_right_radius, maximum_radius);
    rect.bottom_right_radius = std::min(rect.bottom_right_radius, maximum_radius);
    rect.bottom_left_radius = std::min(rect.bottom_left_radius, maximum_radius);
    return rect;
}

[[nodiscard]] PixelRect offset_rect(PixelRect rect, std::int64_t offset) noexcept {
    rect.left += offset;
    rect.top += offset;
    rect.right += offset;
    rect.bottom += offset;
    return rect;
}

// Smoothing interpolates between circular corner coverage and a squircle-like
// fourth-power contour. This is still a bounded raster approximation rather
// than the final ENML vector/path implementation, but it makes smoothing a
// real geometric input instead of silently discarding it.
[[nodiscard]] bool corner_inside(
    std::int64_t pixel_x,
    std::int64_t pixel_y,
    std::int64_t center_x,
    std::int64_t center_y,
    std::int64_t radius,
    std::uint8_t smoothing_percent) noexcept {
    if (radius <= 0) return true;

    const std::int64_t point_x2 = pixel_x * 2 + 1;
    const std::int64_t point_y2 = pixel_y * 2 + 1;
    const std::int64_t center_x2 = center_x * 2;
    const std::int64_t center_y2 = center_y * 2;
    const std::int64_t dx = point_x2 - center_x2;
    const std::int64_t dy = point_y2 - center_y2;
    const std::int64_t radius2 = radius * 2;

    const std::uint64_t dx_sq = static_cast<std::uint64_t>(dx * dx);
    const std::uint64_t dy_sq = static_cast<std::uint64_t>(dy * dy);
    const std::uint64_t radius_sq = static_cast<std::uint64_t>(radius2 * radius2);
    const std::uint64_t circle_metric = (dx_sq + dy_sq) * radius_sq;
    const std::uint64_t squircle_metric = dx_sq * dx_sq + dy_sq * dy_sq;
    const std::uint64_t limit = radius_sq * radius_sq;
    const std::uint64_t smoothing = smoothing_percent;
    const std::uint64_t metric =
        circle_metric * (100U - smoothing) + squircle_metric * smoothing;
    return metric <= limit * 100U;
}

[[nodiscard]] bool contour_contains(
    const PixelRect& rect,
    std::int64_t x,
    std::int64_t y) noexcept {
    if (x < rect.left || x >= rect.right || y < rect.top || y >= rect.bottom) return false;

    if (rect.top_left_radius > 0 &&
        x < rect.left + rect.top_left_radius &&
        y < rect.top + rect.top_left_radius) {
        return corner_inside(
            x, y,
            rect.left + rect.top_left_radius,
            rect.top + rect.top_left_radius,
            rect.top_left_radius,
            rect.smoothing_percent);
    }
    if (rect.top_right_radius > 0 &&
        x >= rect.right - rect.top_right_radius &&
        y < rect.top + rect.top_right_radius) {
        return corner_inside(
            x, y,
            rect.right - rect.top_right_radius,
            rect.top + rect.top_right_radius,
            rect.top_right_radius,
            rect.smoothing_percent);
    }
    if (rect.bottom_right_radius > 0 &&
        x >= rect.right - rect.bottom_right_radius &&
        y >= rect.bottom - rect.bottom_right_radius) {
        return corner_inside(
            x, y,
            rect.right - rect.bottom_right_radius,
            rect.bottom - rect.bottom_right_radius,
            rect.bottom_right_radius,
            rect.smoothing_percent);
    }
    if (rect.bottom_left_radius > 0 &&
        x < rect.left + rect.bottom_left_radius &&
        y >= rect.bottom - rect.bottom_left_radius) {
        return corner_inside(
            x, y,
            rect.left + rect.bottom_left_radius,
            rect.bottom - rect.bottom_left_radius,
            rect.bottom_left_radius,
            rect.smoothing_percent);
    }
    return true;
}

[[nodiscard]] bool corner_inside_subpixel(
    std::int64_t x4,
    std::int64_t y4,
    std::int64_t center_x4,
    std::int64_t center_y4,
    std::int64_t radius4,
    std::uint8_t smoothing_percent) noexcept {
    if (radius4 <= 0) return true;

    const std::int64_t dx = x4 - center_x4;
    const std::int64_t dy = y4 - center_y4;
    const std::uint64_t dx_sq = static_cast<std::uint64_t>(dx * dx);
    const std::uint64_t dy_sq = static_cast<std::uint64_t>(dy * dy);
    const std::uint64_t radius_sq = static_cast<std::uint64_t>(radius4 * radius4);
    const std::uint64_t circle_metric = (dx_sq + dy_sq) * radius_sq;
    const std::uint64_t squircle_metric = dx_sq * dx_sq + dy_sq * dy_sq;
    const std::uint64_t limit = radius_sq * radius_sq;
    const std::uint64_t smoothing = smoothing_percent;
    const std::uint64_t metric =
        circle_metric * (100U - smoothing) + squircle_metric * smoothing;
    return metric <= limit * 100U;
}

[[nodiscard]] bool contour_contains_subpixel(
    const PixelRect& rect,
    std::int64_t x4,
    std::int64_t y4) noexcept {
    const std::int64_t left4 = rect.left * 4;
    const std::int64_t top4 = rect.top * 4;
    const std::int64_t right4 = rect.right * 4;
    const std::int64_t bottom4 = rect.bottom * 4;
    if (x4 < left4 || x4 >= right4 || y4 < top4 || y4 >= bottom4) return false;

    const std::int64_t tl4 = rect.top_left_radius * 4;
    if (tl4 > 0 && x4 < left4 + tl4 && y4 < top4 + tl4) {
        return corner_inside_subpixel(
            x4, y4, left4 + tl4, top4 + tl4, tl4, rect.smoothing_percent);
    }

    const std::int64_t tr4 = rect.top_right_radius * 4;
    if (tr4 > 0 && x4 >= right4 - tr4 && y4 < top4 + tr4) {
        return corner_inside_subpixel(
            x4, y4, right4 - tr4, top4 + tr4, tr4, rect.smoothing_percent);
    }

    const std::int64_t br4 = rect.bottom_right_radius * 4;
    if (br4 > 0 && x4 >= right4 - br4 && y4 >= bottom4 - br4) {
        return corner_inside_subpixel(
            x4, y4, right4 - br4, bottom4 - br4, br4, rect.smoothing_percent);
    }

    const std::int64_t bl4 = rect.bottom_left_radius * 4;
    if (bl4 > 0 && x4 < left4 + bl4 && y4 >= bottom4 - bl4) {
        return corner_inside_subpixel(
            x4, y4, left4 + bl4, bottom4 - bl4, bl4, rect.smoothing_percent);
    }
    return true;
}

[[nodiscard]] std::uint8_t contour_coverage_2x2(
    const PixelRect& rect,
    std::int64_t pixel_x,
    std::int64_t pixel_y) noexcept {
    static constexpr std::array<std::int64_t, 2U> offsets{{1, 3}};
    std::uint8_t samples_inside = 0U;
    for (const auto offset_y : offsets) {
        for (const auto offset_x : offsets) {
            if (contour_contains_subpixel(
                    rect,
                    pixel_x * 4 + offset_x,
                    pixel_y * 4 + offset_y)) {
                ++samples_inside;
            }
        }
    }
    return static_cast<std::uint8_t>(
        (static_cast<std::uint16_t>(samples_inside) * 255U + 2U) / 4U);
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

[[nodiscard]] bool boundary_pixel(
    const PixelRect& rect,
    std::int64_t x,
    std::int64_t y) noexcept {
    if (!contour_contains(rect, x, y)) return false;
    return !contour_contains(rect, x - 1, y) ||
        !contour_contains(rect, x + 1, y) ||
        !contour_contains(rect, x, y - 1) ||
        !contour_contains(rect, x, y + 1);
}

[[nodiscard]] bool leading_light_pixel(
    const PixelRect& rect,
    std::int64_t x,
    std::int64_t y) noexcept {
    if (!boundary_pixel(rect, x, y)) return false;
    return !contour_contains(rect, x - 1, y) || !contour_contains(rect, x, y - 1);
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

[[nodiscard]] bool darken_existing_pixel(
    RasterTarget target,
    std::uint32_t x,
    std::uint32_t y,
    std::uint8_t percent,
    RasterStats& stats) noexcept {
    const std::size_t index = pixel_index(target, x, y);
    const Rgba8 existing = target.pixels[index];
    if (existing.alpha == 0U) return false;
    target.pixels[index] = darken_opaque(existing, percent);
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

        const PixelRect rect = pixel_rect(command, target.scale);
        const Rgba8 fill = resolved_fill(command, theme);

        // Opaque depth fallback: before blur kernels or alpha shadows exist,
        // raised material darkens already-painted supporting pixels at a
        // deterministic positive offset. Transparent/unpainted target pixels
        // are left alone so this stage never invents an opaque backdrop.
        const std::int64_t shadow_offset = scale_radius(
            command.visual.depth.offset_q6,
            target.scale);
        if (fill.alpha != 0U &&
            command.visual.token.material != OpticalMaterialRole::none &&
            command.visual.token.depth != DepthRole::flush &&
            command.visual.depth.opacity_percent != 0U &&
            shadow_offset > 0) {
            const PixelRect shadow_rect = offset_rect(rect, shadow_offset);
            const std::int64_t shadow_left = std::max<std::int64_t>(0, shadow_rect.left);
            const std::int64_t shadow_top = std::max<std::int64_t>(0, shadow_rect.top);
            const std::int64_t shadow_right =
                std::min<std::int64_t>(target.width, shadow_rect.right);
            const std::int64_t shadow_bottom =
                std::min<std::int64_t>(target.height, shadow_rect.bottom);
            bool shadow_written = false;
            for (std::int64_t y = shadow_top; y < shadow_bottom; ++y) {
                for (std::int64_t x = shadow_left; x < shadow_right; ++x) {
                    if (!contour_contains(shadow_rect, x, y)) continue;
                    shadow_written = darken_existing_pixel(
                        target,
                        static_cast<std::uint32_t>(x),
                        static_cast<std::uint32_t>(y),
                        command.visual.depth.opacity_percent,
                        stats) || shadow_written;
                }
            }
            if (shadow_written) ++stats.shadows_drawn;
        }

        const std::int64_t clipped_left = std::max<std::int64_t>(0, rect.left);
        const std::int64_t clipped_top = std::max<std::int64_t>(0, rect.top);
        const std::int64_t clipped_right =
            std::min<std::int64_t>(target.width, rect.right);
        const std::int64_t clipped_bottom =
            std::min<std::int64_t>(target.height, rect.bottom);
        if (clipped_left >= clipped_right || clipped_top >= clipped_bottom) continue;

        bool filled = false;
        if (fill.alpha != 0U) {
            for (std::int64_t y = clipped_top; y < clipped_bottom; ++y) {
                for (std::int64_t x = clipped_left; x < clipped_right; ++x) {
                    if (!contour_contains(rect, x, y)) continue;
                    const std::uint8_t coverage = contour_coverage_2x2(rect, x, y);
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
                    if (!leading_light_pixel(rect, x, y)) continue;
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
                if (!boundary_pixel(rect, x, y)) continue;
                const std::uint8_t coverage = contour_coverage_2x2(rect, x, y);
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
