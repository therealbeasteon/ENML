#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <os/core/result.hpp>
#include <os/ui/types.hpp>

// Layout rules for surfaces that ask the user to decide something.
//
// M4.6 stopped input falling through a surface the user can see. This is the
// other half of the same problem: an event can be obtained from a user who did
// see the surface, because the surface was laid out so that the answer they
// gave was not the answer they meant.
//
// A control too small to hit reliably, two controls close enough that a thumb
// covers both, or an accept button many times the size of the refuse button,
// all produce the same result as tapjacking - a decision recorded that the user
// did not make - without any exploit at all. On an ordinary application screen
// that is bad design and the user's own business. On a surface that grants
// authority, it defeats the trusted path by layout, and none of the boundaries
// underneath it will notice, because the event really did come from the user.
//
// The numbers come from the platform interface guidelines in the references,
// which agree closely: a minimum touch target of 48dp on one platform and
// 44 points on the other, with explicit guidance that adjacent controls need
// spacing between them and not merely size. ENML uses the larger of the two
// as `minimum_touch_target_q6`, already defined in design.hpp - it existed as
// a constant that nothing checked.
//
// The size-ratio rule has no equivalent in those guidelines, because they are
// written to help a designer rather than to constrain an adversary. It is
// ENML's, and it is deliberately crude: on a decision surface, no choice may
// present a hit area more than a small multiple of the smallest choice. That
// refuses the familiar arrangement of a large inviting accept and a hairline
// refuse, which is not a usability defect here but a way of harvesting consent.
namespace os::ui {

// A decision offering more options than this is not a decision the user can
// take in one glance, and a surface that grants authority should not be asking
// it. The ceiling is a rejection criterion, not a buffer size.
inline constexpr std::size_t max_decision_choices = 6U;

// Adjacent interactive targets must be separated by at least this much, on top
// of each being large enough on its own. Two controls that meet exactly at an
// edge are individually reachable and jointly ambiguous.
inline constexpr std::uint32_t minimum_target_separation_q6 = logical_from_dp(8U);

// The widest a choice may be relative to the smallest choice on the same
// surface, as a percentage. Chosen to permit ordinary emphasis - a primary
// action may reasonably be somewhat larger - while refusing the arrangement
// where one option is visually inevitable and the other is a formality.
inline constexpr std::uint32_t maximum_choice_area_ratio_percent = 250U;

enum class ChoiceKind : std::uint8_t {
    // Grants whatever the surface is asking for.
    affirmative = 1U,
    // Refuses. Must always be present on a decision surface.
    negative = 2U,
    // Neither grants nor refuses: more information, a different option.
    neutral = 3U,
};

struct DecisionChoice final {
    ChoiceKind kind {ChoiceKind::neutral};
    LogicalRect bounds {};

    [[nodiscard]] friend bool operator==(const DecisionChoice&, const DecisionChoice&) = default;
};

// Validates the layout of a surface that asks the user to grant or refuse.
//
// Rejects when:
//   - there are no choices, or more than the ceiling;
//   - no negative choice is offered, so refusing requires leaving rather than
//     answering, and a user who wants to say no has no control to press;
//   - any choice is smaller than the minimum touch target in either axis;
//   - any two choices overlap, which makes at least one of them partly
//     unreachable and the boundary between them a matter of z-order;
//   - any two choices are closer than the minimum separation;
//   - the largest choice's area exceeds the smallest choice's area by more than
//     the permitted ratio.
//
// Geometry only. Whether the *words* on the buttons are honest is a different
// problem, and not one a layout check can answer.
[[nodiscard]] os::core::Result<void> validate_decision_layout(
    std::span<const DecisionChoice> choices) noexcept;

} // namespace os::ui
