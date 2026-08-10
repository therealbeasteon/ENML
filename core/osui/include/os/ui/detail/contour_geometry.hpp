#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include <os/ui/raster.hpp>

namespace os::ui::raster_detail {

// Renderer-private physical contour representation shared by the opaque
// material/depth raster and the complementary antialias fringe pass. Keeping a
// single evaluator prevents economy rendering stages from drifting into subtly
// different interpretations of ENML's authored asymmetric curves.
struct PixelContour final {
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

[[nodiscard]] inline std::int64_t scale_floor(
    std::int64_t q6,
    const RasterScale& scale) noexcept {
    return floor_div(
        q6 * static_cast<std::int64_t>(scale.numerator),
        static_cast<std::int64_t>(scale.denominator));
}

[[nodiscard]] inline std::int64_t scale_ceil(
    std::int64_t q6,
    const RasterScale& scale) noexcept {
    return ceil_div(
        q6 * static_cast<std::int64_t>(scale.numerator),
        static_cast<std::int64_t>(scale.denominator));
}

[[nodiscard]] inline std::int64_t scale_radius(
    std::uint32_t q6,
    const RasterScale& scale) noexcept {
    if (q6 == 0U) return 0;
    const std::uint64_t numerator =
        static_cast<std::uint64_t>(q6) * scale.numerator;
    return static_cast<std::int64_t>(
        (numerator + scale.denominator - 1U) / scale.denominator);
}

[[nodiscard]] inline PixelContour pixel_contour(
    const RenderCommand& command,
    const RasterScale& scale) noexcept {
    const std::int64_t left_q6 = command.bounds.x_q6;
    const std::int64_t top_q6 = command.bounds.y_q6;
    const std::int64_t right_q6 =
        left_q6 + static_cast<std::int64_t>(command.bounds.width_q6);
    const std::int64_t bottom_q6 =
        top_q6 + static_cast<std::int64_t>(command.bounds.height_q6);

    PixelContour contour {
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

    const std::int64_t half_width =
        std::max<std::int64_t>(0, (contour.right - contour.left) / 2);
    const std::int64_t half_height =
        std::max<std::int64_t>(0, (contour.bottom - contour.top) / 2);
    const std::int64_t maximum_radius = std::min(half_width, half_height);
    contour.top_left_radius = std::min(contour.top_left_radius, maximum_radius);
    contour.top_right_radius = std::min(contour.top_right_radius, maximum_radius);
    contour.bottom_right_radius = std::min(contour.bottom_right_radius, maximum_radius);
    contour.bottom_left_radius = std::min(contour.bottom_left_radius, maximum_radius);
    return contour;
}

[[nodiscard]] inline PixelContour offset_contour(
    PixelContour contour,
    std::int64_t offset) noexcept {
    contour.left += offset;
    contour.top += offset;
    contour.right += offset;
    contour.bottom += offset;
    return contour;
}

[[nodiscard]] constexpr bool has_curved_corners(const PixelContour& contour) noexcept {
    return contour.top_left_radius != 0 || contour.top_right_radius != 0 ||
        contour.bottom_right_radius != 0 || contour.bottom_left_radius != 0;
}

[[nodiscard]] constexpr std::uint64_t magnitude(std::int64_t value) noexcept {
    if (value >= 0) return static_cast<std::uint64_t>(value);
    return static_cast<std::uint64_t>(-(value + 1)) + 1U;
}

// Evaluate the authored circle→squircle interpolation in a normalized Q10
// coordinate space. The previous direct fourth-power formulation could wrap
// uint64_t for very large but still valid logical contours. Normalizing the
// coordinates before the fourth-power terms keeps every intermediate bounded
// while preserving deterministic integer-only behavior on GCC/Clang and native
// AArch64. Inputs originate from validated LogicalRect/ResolvedContour values.
[[nodiscard]] inline bool corner_inside_normalized(
    std::int64_t x4,
    std::int64_t y4,
    std::int64_t center_x4,
    std::int64_t center_y4,
    std::int64_t radius4,
    std::uint8_t smoothing_percent) noexcept {
    if (radius4 <= 0) return true;

    const std::uint64_t radius = static_cast<std::uint64_t>(radius4);
    const std::uint64_t dx = magnitude(x4 - center_x4);
    const std::uint64_t dy = magnitude(y4 - center_y4);
    if (dx > radius || dy > radius) return false;

    inline constexpr std::uint64_t coordinate_scale = 1024U;
    const std::uint64_t x_q10 =
        (dx * coordinate_scale + radius / 2U) / radius;
    const std::uint64_t y_q10 =
        (dy * coordinate_scale + radius / 2U) / radius;

    const std::uint64_t scale_squared = coordinate_scale * coordinate_scale;
    const std::uint64_t scale_fourth = scale_squared * scale_squared;
    const std::uint64_t x_squared = x_q10 * x_q10;
    const std::uint64_t y_squared = y_q10 * y_q10;
    const std::uint64_t circle_metric =
        (x_squared + y_squared) * scale_squared;
    const std::uint64_t squircle_metric =
        x_squared * x_squared + y_squared * y_squared;
    const std::uint64_t smoothing = smoothing_percent;
    const std::uint64_t metric =
        circle_metric * (100U - smoothing) + squircle_metric * smoothing;
    return metric <= scale_fourth * 100U;
}

[[nodiscard]] inline bool contains_subpixel(
    const PixelContour& contour,
    std::int64_t x4,
    std::int64_t y4) noexcept {
    const std::int64_t left4 = contour.left * 4;
    const std::int64_t top4 = contour.top * 4;
    const std::int64_t right4 = contour.right * 4;
    const std::int64_t bottom4 = contour.bottom * 4;
    if (x4 < left4 || x4 >= right4 || y4 < top4 || y4 >= bottom4) return false;

    const std::int64_t tl4 = contour.top_left_radius * 4;
    if (tl4 > 0 && x4 < left4 + tl4 && y4 < top4 + tl4) {
        return corner_inside_normalized(
            x4, y4, left4 + tl4, top4 + tl4, tl4, contour.smoothing_percent);
    }

    const std::int64_t tr4 = contour.top_right_radius * 4;
    if (tr4 > 0 && x4 >= right4 - tr4 && y4 < top4 + tr4) {
        return corner_inside_normalized(
            x4, y4, right4 - tr4, top4 + tr4, tr4, contour.smoothing_percent);
    }

    const std::int64_t br4 = contour.bottom_right_radius * 4;
    if (br4 > 0 && x4 >= right4 - br4 && y4 >= bottom4 - br4) {
        return corner_inside_normalized(
            x4, y4, right4 - br4, bottom4 - br4, br4, contour.smoothing_percent);
    }

    const std::int64_t bl4 = contour.bottom_left_radius * 4;
    if (bl4 > 0 && x4 < left4 + bl4 && y4 >= bottom4 - bl4) {
        return corner_inside_normalized(
            x4, y4, left4 + bl4, bottom4 - bl4, bl4, contour.smoothing_percent);
    }
    return true;
}

[[nodiscard]] inline bool contains_center(
    const PixelContour& contour,
    std::int64_t pixel_x,
    std::int64_t pixel_y) noexcept {
    return contains_subpixel(contour, pixel_x * 4 + 2, pixel_y * 4 + 2);
}

[[nodiscard]] inline std::uint8_t coverage_2x2(
    const PixelContour& contour,
    std::int64_t pixel_x,
    std::int64_t pixel_y) noexcept {
    static constexpr std::array<std::int64_t, 2U> offsets{{1, 3}};
    std::uint8_t samples_inside = 0U;
    for (const auto offset_y : offsets) {
        for (const auto offset_x : offsets) {
            if (contains_subpixel(
                    contour,
                    pixel_x * 4 + offset_x,
                    pixel_y * 4 + offset_y)) {
                ++samples_inside;
            }
        }
    }
    return static_cast<std::uint8_t>(
        (static_cast<std::uint16_t>(samples_inside) * 255U + 2U) / 4U);
}

[[nodiscard]] inline bool boundary_center(
    const PixelContour& contour,
    std::int64_t x,
    std::int64_t y) noexcept {
    if (!contains_center(contour, x, y)) return false;
    return !contains_center(contour, x - 1, y) ||
        !contains_center(contour, x + 1, y) ||
        !contains_center(contour, x, y - 1) ||
        !contains_center(contour, x, y + 1);
}

[[nodiscard]] inline bool leading_boundary_center(
    const PixelContour& contour,
    std::int64_t x,
    std::int64_t y) noexcept {
    if (!boundary_center(contour, x, y)) return false;
    return !contains_center(contour, x - 1, y) ||
        !contains_center(contour, x, y - 1);
}

} // namespace os::ui::raster_detail
