#pragma once

#include <cstdint>

#include <os/ui/identity.hpp>

namespace os::ui {

enum class MotionDirection : std::uint8_t {
    forward = 0U,
    reverse = 1U,
};

struct MotionContinuity final {
    std::uint32_t progress_q16 {0U};
    std::uint32_t target_q16 {65'535U};
    MotionDirection direction {MotionDirection::forward};
    bool active {false};
};

struct FrameStabilityState final {
    QualityTier admitted_quality {QualityTier::ambient};
    std::uint8_t stable_frames {0U};
    std::uint8_t miss_streak {0U};
};

inline constexpr std::uint32_t motion_one_q16 = 65'535U;
inline constexpr std::uint8_t quality_restore_stable_frames = 8U;

[[nodiscard]] constexpr bool motion_continuity_valid(const MotionContinuity& motion) noexcept {
    return motion.progress_q16 <= motion_one_q16 && motion.target_q16 <= motion_one_q16;
}

// Retarget from the currently visible state. No animation is restarted from an
// old endpoint, so an interrupted gesture does not snap or replay stale travel.
[[nodiscard]] constexpr MotionContinuity retarget_motion(
    MotionContinuity current,
    std::uint32_t target_q16) noexcept {
    if (target_q16 > motion_one_q16 || !motion_continuity_valid(current)) {
        return MotionContinuity{};
    }
    current.target_q16 = target_q16;
    current.direction = target_q16 < current.progress_q16
        ? MotionDirection::reverse : MotionDirection::forward;
    current.active = target_q16 != current.progress_q16;
    return current;
}

[[nodiscard]] constexpr QualityTier next_higher_quality(QualityTier tier) noexcept {
    switch (tier) {
    case QualityTier::essential: return QualityTier::continuity;
    case QualityTier::continuity: return QualityTier::material;
    case QualityTier::material: return QualityTier::depth;
    case QualityTier::depth: return QualityTier::ambient;
    case QualityTier::ambient: return QualityTier::ambient;
    }
    return QualityTier::essential;
}

// Degradation is immediate; restoration is deliberately gradual. This avoids
// frame-to-frame quality oscillation after a hitch, thermal event, or GPU spike.
[[nodiscard]] constexpr FrameStabilityState update_frame_stability(
    FrameStabilityState state,
    QualityTier requested_maximum,
    bool missed_deadline) noexcept {
    if (missed_deadline) {
        state.stable_frames = 0U;
        if (state.miss_streak < 255U) ++state.miss_streak;
        state.admitted_quality = clamp_quality(state.admitted_quality, requested_maximum);
        state.admitted_quality = clamp_quality(state.admitted_quality, QualityTier::continuity);
        return state;
    }

    state.miss_streak = 0U;
    if (state.stable_frames < 255U) ++state.stable_frames;

    state.admitted_quality = clamp_quality(state.admitted_quality, requested_maximum);
    if (state.stable_frames >= quality_restore_stable_frames &&
        static_cast<std::uint8_t>(state.admitted_quality) <
            static_cast<std::uint8_t>(requested_maximum)) {
        state.admitted_quality = next_higher_quality(state.admitted_quality);
        state.stable_frames = 0U;
    }
    return state;
}

} // namespace os::ui
