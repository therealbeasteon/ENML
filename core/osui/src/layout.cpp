#include <os/ui/types.hpp>

#include <algorithm>
#include <cstdint>

#include <os/ui/error.hpp>

namespace os::ui {
namespace {

[[nodiscard]] bool policy_valid(const ResponsivePolicy& policy) noexcept {
    return policy.two_pane_min_width_q6 != 0U &&
        policy.pane_gap_q6 <= max_logical_dimension_q6 &&
        policy.primary_min_width_q6 != 0U &&
        policy.primary_max_width_q6 >= policy.primary_min_width_q6 &&
        policy.primary_max_width_q6 <= max_logical_dimension_q6 &&
        policy.secondary_min_width_q6 != 0U &&
        policy.secondary_min_width_q6 <= max_logical_dimension_q6 &&
        policy.primary_share_numerator != 0U &&
        policy.primary_share_denominator != 0U &&
        policy.primary_share_numerator <= policy.primary_share_denominator;
}

[[nodiscard]] LogicalRect safe_content(const LogicalViewport& viewport) noexcept {
    const std::uint32_t width = viewport.width_q6 - viewport.safe_insets.left_q6 -
        viewport.safe_insets.right_q6;
    const std::uint32_t height = viewport.height_q6 - viewport.safe_insets.top_q6 -
        viewport.safe_insets.bottom_q6;
    return LogicalRect{
        .x_q6 = static_cast<std::int32_t>(viewport.safe_insets.left_q6),
        .y_q6 = static_cast<std::int32_t>(viewport.safe_insets.top_q6),
        .width_q6 = width,
        .height_q6 = height,
    };
}

[[nodiscard]] PaneLayout single_pane(LogicalRect content) noexcept {
    return PaneLayout{
        .mode = PaneLayoutMode::single,
        .primary = content,
        .secondary = {},
    };
}

} // namespace

os::core::Result<PaneLayout> layout_list_detail(
    const LogicalViewport& viewport,
    const ResponsivePolicy& policy) noexcept {
    if (!viewport.valid() || !policy_valid(policy)) {
        return ui_error(errors::invalid_viewport);
    }

    const LogicalRect content = safe_content(viewport);
    if (!content.bounded()) return ui_error(errors::invalid_viewport);
    if (content.width_q6 < policy.two_pane_min_width_q6) {
        return single_pane(content);
    }

    const std::uint64_t minimum_required =
        static_cast<std::uint64_t>(policy.primary_min_width_q6) +
        static_cast<std::uint64_t>(policy.pane_gap_q6) +
        static_cast<std::uint64_t>(policy.secondary_min_width_q6);
    if (minimum_required > static_cast<std::uint64_t>(content.width_q6)) {
        return single_pane(content);
    }

    const std::uint64_t shared =
        static_cast<std::uint64_t>(content.width_q6) *
        static_cast<std::uint64_t>(policy.primary_share_numerator) /
        static_cast<std::uint64_t>(policy.primary_share_denominator);
    std::uint32_t primary_width = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(shared, static_cast<std::uint64_t>(max_logical_dimension_q6)));
    primary_width = std::clamp(
        primary_width,
        policy.primary_min_width_q6,
        policy.primary_max_width_q6);

    const std::uint64_t needed_with_primary =
        static_cast<std::uint64_t>(primary_width) +
        static_cast<std::uint64_t>(policy.pane_gap_q6) +
        static_cast<std::uint64_t>(policy.secondary_min_width_q6);
    if (needed_with_primary > static_cast<std::uint64_t>(content.width_q6)) {
        const std::uint32_t adjusted = content.width_q6 - policy.pane_gap_q6 -
            policy.secondary_min_width_q6;
        if (adjusted < policy.primary_min_width_q6) return single_pane(content);
        primary_width = adjusted;
    }

    const std::uint32_t secondary_width = content.width_q6 - primary_width - policy.pane_gap_q6;
    if (secondary_width < policy.secondary_min_width_q6) return single_pane(content);

    const std::int64_t secondary_x = static_cast<std::int64_t>(content.x_q6) +
        static_cast<std::int64_t>(primary_width) +
        static_cast<std::int64_t>(policy.pane_gap_q6);
    if (secondary_x > static_cast<std::int64_t>(max_logical_dimension_q6)) {
        return ui_error(errors::invalid_viewport);
    }

    const LogicalRect primary{
        .x_q6 = content.x_q6,
        .y_q6 = content.y_q6,
        .width_q6 = primary_width,
        .height_q6 = content.height_q6,
    };
    const LogicalRect secondary{
        .x_q6 = static_cast<std::int32_t>(secondary_x),
        .y_q6 = content.y_q6,
        .width_q6 = secondary_width,
        .height_q6 = content.height_q6,
    };
    if (!primary.bounded() || !secondary.bounded()) {
        return ui_error(errors::invalid_viewport);
    }

    return PaneLayout{
        .mode = PaneLayoutMode::dual,
        .primary = primary,
        .secondary = secondary,
    };
}

} // namespace os::ui
