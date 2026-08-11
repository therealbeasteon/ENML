#include <os/ui/decision.hpp>

#include <os/core/error.hpp>
#include <os/ui/design.hpp>
#include <os/ui/error.hpp>

namespace os::ui {
namespace {

[[nodiscard]] constexpr os::core::Error ui_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ui, code);
}

struct Extent final {
    std::int64_t left {0};
    std::int64_t top {0};
    std::int64_t right {0};
    std::int64_t bottom {0};
};

[[nodiscard]] Extent extent_of(const LogicalRect& rect) noexcept {
    const auto left = static_cast<std::int64_t>(rect.x_q6);
    const auto top = static_cast<std::int64_t>(rect.y_q6);
    return Extent{
        left,
        top,
        left + static_cast<std::int64_t>(rect.width_q6),
        top + static_cast<std::int64_t>(rect.height_q6),
    };
}

// The gap between two extents along one axis, or -1 when they overlap on it.
[[nodiscard]] std::int64_t axis_gap(
    std::int64_t a_low,
    std::int64_t a_high,
    std::int64_t b_low,
    std::int64_t b_high) noexcept {
    if (a_high <= b_low) return b_low - a_high;
    if (b_high <= a_low) return a_low - b_high;
    return -1;
}

} // namespace

os::core::Result<void> validate_decision_layout(
    std::span<const DecisionChoice> choices) noexcept {
    if (choices.empty() || choices.size() > max_decision_choices) {
        return ui_error(errors::invalid_decision_layout);
    }

    bool has_negative = false;
    std::uint64_t smallest_area = 0U;
    std::uint64_t largest_area = 0U;

    for (std::size_t index = 0U; index < choices.size(); ++index) {
        const DecisionChoice& choice = choices[index];

        // Unknown kinds are rejected rather than treated as neutral, since a
        // kind nobody handles is one the negative-choice check below would
        // silently skip.
        if (choice.kind != ChoiceKind::affirmative && choice.kind != ChoiceKind::negative &&
            choice.kind != ChoiceKind::neutral) {
            return ui_error(errors::invalid_decision_layout);
        }
        if (!choice.bounds.bounded()) {
            return ui_error(errors::invalid_decision_layout);
        }
        if (choice.kind == ChoiceKind::negative) {
            has_negative = true;
        }

        // Both axes, not area: a control can have ample area and still be a
        // sliver too thin to hit.
        if (choice.bounds.width_q6 < minimum_touch_target_q6 ||
            choice.bounds.height_q6 < minimum_touch_target_q6) {
            return ui_error(errors::undersized_decision_target);
        }

        const auto area = static_cast<std::uint64_t>(choice.bounds.width_q6) *
            static_cast<std::uint64_t>(choice.bounds.height_q6);
        if (index == 0U || area < smallest_area) smallest_area = area;
        if (index == 0U || area > largest_area) largest_area = area;

        for (std::size_t other = 0U; other < index; ++other) {
            const auto a = extent_of(choice.bounds);
            const auto b = extent_of(choices[other].bounds);
            const auto horizontal = axis_gap(a.left, a.right, b.left, b.right);
            const auto vertical = axis_gap(a.top, a.bottom, b.top, b.bottom);

            // Overlapping on both axes means the rectangles intersect. Part of
            // at least one control is unreachable, and which one receives a
            // press in the shared region is a question about z-order rather
            // than about what the user aimed at.
            if (horizontal < 0 && vertical < 0) {
                return ui_error(errors::overlapping_decision_targets);
            }

            // Separated on both axes: diagonal neighbours that share no edge,
            // which no single press can straddle. Separated on exactly one:
            // they face each other across that axis and need clearance.
            const auto separation = horizontal >= 0 && vertical >= 0
                ? (horizontal > vertical ? horizontal : vertical)
                : (horizontal >= 0 ? horizontal : vertical);
            if (separation < static_cast<std::int64_t>(minimum_target_separation_q6)) {
                return ui_error(errors::insufficient_target_separation);
            }
        }
    }

    // Refusing must be something the user can do by answering, not only by
    // abandoning the surface. A decision with no negative choice is not a
    // decision.
    if (!has_negative) {
        return ui_error(errors::missing_negative_choice);
    }

    // Compared as area against the smallest choice. Multiplied out rather than
    // divided so the ratio is exact and no rounding can be arranged to sit
    // just inside the limit.
    if (largest_area * 100U >
        smallest_area * static_cast<std::uint64_t>(maximum_choice_area_ratio_percent)) {
        return ui_error(errors::disproportionate_decision_choice);
    }

    return {};
}

} // namespace os::ui
