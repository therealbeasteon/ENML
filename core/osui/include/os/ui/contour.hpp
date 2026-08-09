#pragma once

#include <algorithm>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/ui/design.hpp>
#include <os/ui/error.hpp>
#include <os/ui/types.hpp>

namespace os::ui {

struct CornerRadii final {
    std::uint32_t top_left_q6 {0U};
    std::uint32_t top_right_q6 {0U};
    std::uint32_t bottom_right_q6 {0U};
    std::uint32_t bottom_left_q6 {0U};

    [[nodiscard]] friend constexpr bool operator==(
        const CornerRadii&,
        const CornerRadii&) = default;
};

// ResolvedContour is renderer-facing geometry intent, not a vendor path ABI.
// Smoothing is retained separately so a later software/GPU renderer can
// realize continuous curvature without forcing every role into circular arcs.
struct ResolvedContour final {
    CurveRole role {CurveRole::rectilinear};
    CornerRadii radii {};
    std::uint8_t smoothing_percent {0U};
    bool asymmetric {false};
};

[[nodiscard]] inline os::core::Result<ResolvedContour> resolve_contour(
    const LogicalRect& bounds,
    CurveRole role) noexcept {
    if (!bounds.bounded()) return ui_error(errors::invalid_bounds);

    auto metrics = curve_metrics(role);
    if (!metrics) return metrics.error();

    const std::uint32_t half_short_side =
        std::min(bounds.width_q6, bounds.height_q6) / 2U;

    if (role == CurveRole::rectilinear) {
        return ResolvedContour{
            .role = role,
            .radii = {},
            .smoothing_percent = metrics.value().smoothing_percent,
            .asymmetric = false,
        };
    }

    if (role == CurveRole::capsule) {
        return ResolvedContour{
            .role = role,
            .radii = CornerRadii{
                .top_left_q6 = half_short_side,
                .top_right_q6 = half_short_side,
                .bottom_right_q6 = half_short_side,
                .bottom_left_q6 = half_short_side,
            },
            .smoothing_percent = metrics.value().smoothing_percent,
            .asymmetric = false,
        };
    }

    const std::uint32_t base =
        std::min(metrics.value().nominal_radius_q6, half_short_side);

    if (role == CurveRole::swept) {
        const auto scaled = [](std::uint32_t value, std::uint32_t numerator,
                               std::uint32_t denominator) noexcept {
            return static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(value) * numerator / denominator);
        };
        const std::uint32_t bottom_left = std::min<std::uint32_t>(
            half_short_side,
            scaled(base, 5U, 4U));
        return ResolvedContour{
            .role = role,
            .radii = CornerRadii{
                .top_left_q6 = scaled(base, 1U, 2U),
                .top_right_q6 = base,
                .bottom_right_q6 = scaled(base, 3U, 4U),
                .bottom_left_q6 = bottom_left,
            },
            .smoothing_percent = metrics.value().smoothing_percent,
            .asymmetric = true,
        };
    }

    return ResolvedContour{
        .role = role,
        .radii = CornerRadii{
            .top_left_q6 = base,
            .top_right_q6 = base,
            .bottom_right_q6 = base,
            .bottom_left_q6 = base,
        },
        .smoothing_percent = metrics.value().smoothing_percent,
        .asymmetric = false,
    };
}

} // namespace os::ui
