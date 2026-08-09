#include <os/ui/motion.hpp>

#include <cstdint>
#include <limits>

#include <os/ui/error.hpp>

namespace os::ui {
namespace {

inline constexpr std::uint64_t ns_per_ms = 1'000'000ULL;
inline constexpr std::uint64_t q16_one = motion_progress_one_q16;

[[nodiscard]] constexpr std::uint32_t clamp_q16(std::uint64_t value) noexcept {
    return static_cast<std::uint32_t>(value > q16_one ? q16_one : value);
}

[[nodiscard]] constexpr std::uint32_t square_q16(std::uint32_t value) noexcept {
    const auto wide = static_cast<std::uint64_t>(value) * static_cast<std::uint64_t>(value);
    return clamp_q16((wide + (q16_one / 2U)) / q16_one);
}

[[nodiscard]] constexpr std::uint32_t cubic_q16(std::uint32_t value) noexcept {
    const auto squared = square_q16(value);
    const auto wide = static_cast<std::uint64_t>(squared) * static_cast<std::uint64_t>(value);
    return clamp_q16((wide + (q16_one / 2U)) / q16_one);
}

[[nodiscard]] constexpr std::uint32_t curve_progress(
    MotionCurve curve,
    std::uint32_t linear_q16) noexcept {
    const auto p = linear_q16 > motion_progress_one_q16
        ? motion_progress_one_q16 : linear_q16;
    const auto inverse = motion_progress_one_q16 - p;

    switch (curve) {
    case MotionCurve::linear:
        return p;
    case MotionCurve::ease_out:
        return motion_progress_one_q16 - square_q16(inverse);
    case MotionCurve::ease_in_out: {
        // Smoothstep: 3p^2 - 2p^3. Integer-only and bounded so this path
        // remains deterministic across host and ARM64 validation tiers.
        const auto p2 = static_cast<std::uint64_t>(square_q16(p));
        const auto p3 = static_cast<std::uint64_t>(cubic_q16(p));
        const auto value = 3ULL * p2 >= 2ULL * p3 ? 3ULL * p2 - 2ULL * p3 : 0ULL;
        return clamp_q16(value);
    }
    case MotionCurve::spring_soft:
        // Lightweight non-overshooting fallback. A future compositor/GPU
        // backend may provide a physically richer spring while preserving the
        // semantic duration and the no-background-work scheduling contract.
        return motion_progress_one_q16 - cubic_q16(inverse);
    case MotionCurve::spring_precise:
        return motion_progress_one_q16 - square_q16(inverse);
    }
    return p;
}

[[nodiscard]] bool timeline_valid(const MotionTimeline& timeline) noexcept {
    if (timeline.metrics.duration_ms > max_motion_duration_ms) return false;
    if (!timeline.active) {
        return timeline.metrics.duration_ms == 0U && timeline.end_ns == timeline.start_ns;
    }
    if (timeline.metrics.duration_ms == 0U || timeline.end_ns <= timeline.start_ns) return false;
    const auto duration_ns =
        static_cast<std::uint64_t>(timeline.metrics.duration_ms) * ns_per_ms;
    return timeline.end_ns - timeline.start_ns == duration_ns;
}

} // namespace

os::core::Result<MotionTimeline> begin_motion(
    MotionRole role,
    bool reduce_motion,
    std::uint64_t now_ns) noexcept {
    auto metrics_result = motion_metrics(role, reduce_motion);
    if (!metrics_result) return metrics_result.error();

    const MotionMetrics metrics = metrics_result.value();
    if (metrics.duration_ms > max_motion_duration_ms) {
        return ui_error(errors::invalid_motion_timeline);
    }
    if (metrics.duration_ms == 0U) {
        return MotionTimeline{
            .metrics = metrics,
            .start_ns = now_ns,
            .end_ns = now_ns,
            .active = false,
        };
    }

    const auto duration_ns = static_cast<std::uint64_t>(metrics.duration_ms) * ns_per_ms;
    if (now_ns > std::numeric_limits<std::uint64_t>::max() - duration_ns) {
        return ui_error(errors::invalid_motion_timeline);
    }

    return MotionTimeline{
        .metrics = metrics,
        .start_ns = now_ns,
        .end_ns = now_ns + duration_ns,
        .active = true,
    };
}

os::core::Result<MotionFrameSample> sample_motion(
    const MotionTimeline& timeline,
    std::uint64_t now_ns,
    std::uint64_t next_compositor_tick_ns) noexcept {
    if (!timeline_valid(timeline)) {
        return ui_error(errors::invalid_motion_timeline);
    }

    if (!timeline.active) return MotionFrameSample{};
    if (now_ns < timeline.start_ns) {
        return ui_error(errors::invalid_motion_timeline);
    }
    if (now_ns >= timeline.end_ns) {
        return MotionFrameSample{
            .progress_q16 = motion_progress_one_q16,
            .complete = true,
            .request_next_frame = false,
            .next_frame_ns = 0U,
        };
    }

    const auto duration_ns = timeline.end_ns - timeline.start_ns;
    const auto elapsed_ns = now_ns - timeline.start_ns;
    const auto linear = static_cast<std::uint32_t>(
        (elapsed_ns * static_cast<std::uint64_t>(motion_progress_one_q16)) /
        duration_ns);

    MotionFrameSample sample{
        .progress_q16 = curve_progress(timeline.metrics.curve, linear),
        .complete = false,
        .request_next_frame = false,
        .next_frame_ns = 0U,
    };

    if (next_compositor_tick_ns <= now_ns) {
        // Do not spin or invent an independent timer when the compositor has
        // not supplied a usable future tick.
        return sample;
    }

    sample.request_next_frame = true;
    sample.next_frame_ns = next_compositor_tick_ns < timeline.end_ns
        ? next_compositor_tick_ns : timeline.end_ns;
    return sample;
}

} // namespace os::ui
