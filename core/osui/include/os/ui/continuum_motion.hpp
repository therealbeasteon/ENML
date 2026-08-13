#pragma once

#include <cstdint>

#include <os/ui/cookie_continuum.hpp>

namespace os::ui {

enum class ContinuumTransitionKind : std::uint8_t {
    node_to_app = 0U,
    app_to_node = 1U,
    index_reveal = 2U,
    index_retract = 3U,
    task_handoff = 4U,
};

struct ContinuumTransition final {
    ContinuumTransitionKind kind {ContinuumTransitionKind::node_to_app};
    std::uint64_t source_object_id {0U};
    std::uint16_t progress_q10 {0U};
    std::int16_t velocity_q10 {0};
    bool preserve_source_geometry {true};
    bool interruptible {true};
};

[[nodiscard]] constexpr bool continuum_transition_valid(const ContinuumTransition& transition) noexcept {
    if (transition.progress_q10 > 1024U) return false;
    if (!transition.interruptible) return false;
    if ((transition.kind == ContinuumTransitionKind::node_to_app ||
         transition.kind == ContinuumTransitionKind::app_to_node ||
         transition.kind == ContinuumTransitionKind::task_handoff) &&
        transition.source_object_id == 0U) return false;
    if ((transition.kind == ContinuumTransitionKind::node_to_app ||
         transition.kind == ContinuumTransitionKind::app_to_node) &&
        !transition.preserve_source_geometry) return false;
    return true;
}

[[nodiscard]] constexpr ContinuumTransition retarget_continuum_transition(
    ContinuumTransition current,
    ContinuumTransitionKind target) noexcept {
    current.kind = target;
    // Retarget from the visible state. Do not reset progress or velocity: this
    // preserves continuity when the user reverses or changes intent mid-flight.
    if (target == ContinuumTransitionKind::app_to_node ||
        target == ContinuumTransitionKind::index_retract) {
        if (current.velocity_q10 > 0) current.velocity_q10 = static_cast<std::int16_t>(-current.velocity_q10);
    } else if (current.velocity_q10 < 0) {
        current.velocity_q10 = static_cast<std::int16_t>(-current.velocity_q10);
    }
    return current;
}

[[nodiscard]] constexpr bool continuum_transition_complete(const ContinuumTransition& transition) noexcept {
    return transition.progress_q10 == 1024U;
}

} // namespace os::ui
