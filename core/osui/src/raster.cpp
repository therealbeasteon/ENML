#include <os/ui/raster.hpp>

#include <algorithm>
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

[[nodiscard]] bool corner_inside(
    std::int64_t pixel_x,
    std::int64_t pixel_y,
    std::int64_t center_x,
    std::int64_t center_y,
    std::int64_t radius) noexcept {
    if (radius <= 0) return true;
    const std::int64_t point_x2 = pixel_x * 2 + 1;
    const std::int64_t point_y2 = pixel_y * 2 + 1;
    const std::int64_t center_x2 = center_x * 2;
    const std::int64_t center_y2 = center_y * 2;
    const std::int64_t dx = point_x2 - center_x2;
    const std::int64_t dy = point_y2 - center_y2;
    const std::int64_t radius2 = radius * 2;
    return dx * dx + dy * dy <= radius2 * radius2;
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
            rect.top_left_radius);
    }
    if (rect.top_right_radius > 0 &&
        x >= rect.right - rect.top_right_radius &&
        y < rect.top + rect.top_right_radius) {
        return corner_inside(
            x, y,
            rect.right - rect.top_right_radius,
            rect.top + rect.top_right_radius,
            rect.top_right_radius);
    }
    if (rect.bottom_right_radius > 0 &&
        x >= rect.right - rect.bottom_right_radius &&
        y >= rect.bottom - rect.bottom_right_radius) {
        return corner_inside(
            x, y,
            rect.right - rect.bottom_right_radius,
            rect.bottom - rect.bottom_right_radius,
            rect.bottom_right_radius);
    }
    if (rect.bottom_left_radius > 0 &&
        x < rect.left + rect.bottom_left_radius &&
        y >= rect.bottom - rect.bottom_left_radius) {
        return corner_inside(
            x, y,
            rect.left + rect.bottom_left_radius,
            rect.bottom - rect.bottom_left_radius,
            rect.bottom_left_radius);
    }
    return true;
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

[[nodiscard]] bool command_valid(const RenderCommand& command) noexcept {
    return command.source.value() != 0U && command.bounds.bounded() &&
        color_role_valid(command.visual.token.background) &&
        color_role_valid(command.visual.token.material_tint) &&
        color_role_valid(command.visual.token.outline) &&
        command.visual.material.tint_percent <= 100U &&
        command.visual.material.opacity_percent <= 100U &&
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

void write_pixel(
    RasterTarget target,
    std::uint32_t x,
    std::uint32_t y,
    Rgba8 color,
    RasterStats& stats) noexcept {
    const std::size_t index =
        static_cast<std::size_t>(y) * target.stride + static_cast<std::size_t>(x);
    target.pixels[index] = color;
    ++stats.pixel_writes;
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
        const std::int64_t clipped_left = std::max<std::int64_t>(0, rect.left);
        const std::int64_t clipped_top = std::max<std::int64_t>(0, rect.top);
        const std::int64_t clipped_right =
            std::min<std::int64_t>(target.width, rect.right);
        const std::int64_t clipped_bottom =
            std::min<std::int64_t>(target.height, rect.bottom);
        if (clipped_left >= clipped_right || clipped_top >= clipped_bottom) continue;

        const Rgba8 fill = resolved_fill(command, theme);
        bool filled = false;
        if (fill.alpha != 0U) {
            for (std::int64_t y = clipped_top; y < clipped_bottom; ++y) {
                for (std::int64_t x = clipped_left; x < clipped_right; ++x) {
                    if (!contour_contains(rect, x, y)) continue;
                    write_pixel(
                        target,
                        static_cast<std::uint32_t>(x),
                        static_cast<std::uint32_t>(y),
                        fill,
                        stats);
                    filled = true;
                }
            }
        }
        if (filled) ++stats.surfaces_filled;

        const ColorRole outline_role =
            command.focus_visible ? ColorRole::focus : command.visual.token.outline;
        if (outline_role == ColorRole::transparent) continue;
        const Rgba8 outline = theme.colors[color_index(outline_role)];
        for (std::int64_t y = clipped_top; y < clipped_bottom; ++y) {
            for (std::int64_t x = clipped_left; x < clipped_right; ++x) {
                if (!boundary_pixel(rect, x, y)) continue;
                write_pixel(
                    target,
                    static_cast<std::uint32_t>(x),
                    static_cast<std::uint32_t>(y),
                    outline,
                    stats);
            }
        }
    }
    return stats;
}

} // namespace os::ui
