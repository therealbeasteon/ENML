#include <array>
#include <cstdint>
#include <cstdio>

#include <os/core/error.hpp>
#include <os/ui/decision.hpp>
#include <os/ui/design.hpp>
#include <os/ui/error.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "decision layout: %s\n", what);
    }
    return condition;
}

bool refused(const os::core::Result<void>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::ui &&
        result.error().code == code;
}

constexpr std::uint32_t target = os::ui::minimum_touch_target_q6;
constexpr std::uint32_t gap = os::ui::minimum_target_separation_q6;

constexpr os::ui::DecisionChoice choice(
    os::ui::ChoiceKind kind,
    std::int32_t x,
    std::int32_t y,
    std::uint32_t width,
    std::uint32_t height) {
    return os::ui::DecisionChoice{kind, os::ui::LogicalRect{x, y, width, height}};
}

} // namespace

int main() {
    // A well-formed decision: two equally weighted choices, each at the
    // minimum target size, separated by the minimum clearance.
    const std::array good{
        choice(os::ui::ChoiceKind::negative, 0, 0, target, target),
        choice(os::ui::ChoiceKind::affirmative,
               static_cast<std::int32_t>(target + gap), 0, target, target),
    };
    if (!check(static_cast<bool>(os::ui::validate_decision_layout(good)),
               "well-formed layout refused")) return 1;

    // No choices, and more choices than a decision surface may ask.
    if (!check(refused(os::ui::validate_decision_layout({}),
                       os::ui::errors::invalid_decision_layout),
               "empty layout accepted")) return 1;
    {
        std::array<os::ui::DecisionChoice, os::ui::max_decision_choices + 1U> many{};
        for (std::size_t index = 0U; index < many.size(); ++index) {
            many[index] = choice(
                index == 0U ? os::ui::ChoiceKind::negative : os::ui::ChoiceKind::neutral,
                static_cast<std::int32_t>(index * (target + gap)), 0, target, target);
        }
        if (!check(refused(os::ui::validate_decision_layout(many),
                           os::ui::errors::invalid_decision_layout),
                   "too many choices accepted")) return 1;
    }

    // Refusing must be possible by answering, not only by abandoning.
    const std::array no_refusal{
        choice(os::ui::ChoiceKind::affirmative, 0, 0, target, target),
        choice(os::ui::ChoiceKind::neutral,
               static_cast<std::int32_t>(target + gap), 0, target, target),
    };
    if (!check(refused(os::ui::validate_decision_layout(no_refusal),
                       os::ui::errors::missing_negative_choice),
               "decision without a negative choice accepted")) return 1;

    // Undersized in either axis alone, not only in area. A sliver can have
    // ample area and still be impossible to hit.
    const std::array thin{
        choice(os::ui::ChoiceKind::negative, 0, 0, target, target - 1U),
        choice(os::ui::ChoiceKind::affirmative,
               static_cast<std::int32_t>(target + gap), 0, target, target),
    };
    if (!check(refused(os::ui::validate_decision_layout(thin),
                       os::ui::errors::undersized_decision_target),
               "short target accepted")) return 1;
    const std::array narrow{
        choice(os::ui::ChoiceKind::negative, 0, 0, target - 1U, target),
        choice(os::ui::ChoiceKind::affirmative,
               static_cast<std::int32_t>(target + gap), 0, target, target),
    };
    if (!check(refused(os::ui::validate_decision_layout(narrow),
                       os::ui::errors::undersized_decision_target),
               "narrow target accepted")) return 1;

    // Overlapping controls: part of one is unreachable, and the shared region
    // belongs to whichever happens to be on top.
    const std::array overlapping{
        choice(os::ui::ChoiceKind::negative, 0, 0, target * 2U, target),
        choice(os::ui::ChoiceKind::affirmative,
               static_cast<std::int32_t>(target), 0, target * 2U, target),
    };
    if (!check(refused(os::ui::validate_decision_layout(overlapping),
                       os::ui::errors::overlapping_decision_targets),
               "overlapping targets accepted")) return 1;

    // Touching exactly at an edge is individually reachable and jointly
    // ambiguous, so clearance is required on top of size.
    const std::array touching{
        choice(os::ui::ChoiceKind::negative, 0, 0, target, target),
        choice(os::ui::ChoiceKind::affirmative,
               static_cast<std::int32_t>(target), 0, target, target),
    };
    if (!check(refused(os::ui::validate_decision_layout(touching),
                       os::ui::errors::insufficient_target_separation),
               "abutting targets accepted")) return 1;
    const std::array nearly{
        choice(os::ui::ChoiceKind::negative, 0, 0, target, target),
        choice(os::ui::ChoiceKind::affirmative,
               static_cast<std::int32_t>(target + gap - 1U), 0, target, target),
    };
    if (!check(refused(os::ui::validate_decision_layout(nearly),
                       os::ui::errors::insufficient_target_separation),
               "target one unit short of clearance accepted")) return 1;

    // Diagonal neighbours share no edge, so no single press can straddle them.
    const std::array diagonal{
        choice(os::ui::ChoiceKind::negative, 0, 0, target, target),
        choice(os::ui::ChoiceKind::affirmative,
               static_cast<std::int32_t>(target + gap),
               static_cast<std::int32_t>(target + gap), target, target),
    };
    if (!check(static_cast<bool>(os::ui::validate_decision_layout(diagonal)),
               "diagonal neighbours refused")) return 1;

    // The rule this file exists for: a large inviting accept beside a
    // technically-compliant refuse. Both meet the minimum target size, and the
    // layout is still a way of harvesting consent.
    const std::array lopsided{
        choice(os::ui::ChoiceKind::negative, 0, 0, target, target),
        choice(os::ui::ChoiceKind::affirmative,
               static_cast<std::int32_t>(target + gap), 0, target * 4U, target),
    };
    if (!check(refused(os::ui::validate_decision_layout(lopsided),
                       os::ui::errors::disproportionate_decision_choice),
               "disproportionate accept accepted")) return 1;

    // Ordinary emphasis remains allowed. The rule refuses inevitability, not
    // hierarchy, so a primary action may be somewhat larger.
    const std::array emphasised{
        choice(os::ui::ChoiceKind::negative, 0, 0, target, target),
        choice(os::ui::ChoiceKind::affirmative,
               static_cast<std::int32_t>(target + gap), 0, target * 2U, target),
    };
    if (!check(static_cast<bool>(os::ui::validate_decision_layout(emphasised)),
               "reasonable emphasis refused")) return 1;

    // The ratio is on area, so the same imbalance achieved in two axes at once
    // is caught too - one and a half times in each direction is well past the
    // limit in area even though neither axis looks extreme.
    const std::array both_axes{
        choice(os::ui::ChoiceKind::negative, 0, 0, target, target),
        choice(os::ui::ChoiceKind::affirmative,
               static_cast<std::int32_t>(target + gap), 0,
               (target * 3U) / 2U, (target * 3U) / 2U),
    };
    if (!check(static_cast<bool>(os::ui::validate_decision_layout(both_axes)) ==
                   ((9U * 100U) <= (4U * os::ui::maximum_choice_area_ratio_percent)),
               "area ratio not applied consistently across both axes")) return 1;

    return 0;
}
