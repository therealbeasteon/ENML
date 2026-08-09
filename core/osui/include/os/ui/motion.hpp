#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/ui/design.hpp>

namespace os::ui {

// ENML motion is sampled only when the compositor/render loop provides a
// timing opportunity. This layer owns no worker thread, timer thread, polling
// loop or unbounded queue. It exists to keep animation responsive without
// creating idle wakeups when nothing is moving.
inline constexpr std::uint32_t motion_progress_one_q16 = 65'535U;
inline constexpr std::uint16_t max_motion_duration_ms = 5'000U;

struct MotionTimeline final {
    MotionMetrics metrics {};
    std::uint64_t start_ns {0U};
    std::uint64_t end_ns {0U};
    bool active {false};
};

struct MotionFrameSample final {
    std::uint32_t progress_q16 {motion_progress_one_q16};
    bool complete {true};
    bool request_next_frame {false};
    std::uint64_t next_frame_ns {0U};
};

// Builds one bounded timeline from the semantic motion role. Reduced-motion
// preferences are resolved by the existing design system before timing is
// frozen into the timeline.
[[nodiscard]] os::core::Result<MotionTimeline> begin_motion(
    MotionRole role,
    bool reduce_motion,
    std::uint64_t now_ns) noexcept;

// Samples a timeline using caller-provided monotonic time and the next known
// compositor tick. At most one future frame is requested. A stale/absent tick
// produces no retry loop: the caller waits for a future compositor opportunity.
[[nodiscard]] os::core::Result<MotionFrameSample> sample_motion(
    const MotionTimeline& timeline,
    std::uint64_t now_ns,
    std::uint64_t next_compositor_tick_ns) noexcept;

} // namespace os::ui
