#pragma once

#include <cstdint>
#include <limits>

#include <os/ui/identity.hpp>
#include <os/ui/quality_policy.hpp>

namespace os::ui {

struct FrameTelemetry final {
    std::uint32_t refresh_hz {60U};
    std::uint64_t input_age_ns {0U};
    std::uint64_t cpu_render_ns {0U};
    std::uint64_t gpu_render_ns {0U};
    std::uint64_t present_wait_ns {0U};
    std::uint8_t consecutive_misses {0U};
    bool direct_manipulation_active {false};
};

struct FrameScheduleDecision final {
    QualityTier maximum_quality {QualityTier::ambient};
    bool preserve_spatial_motion {true};
    bool prioritize_direct_manipulation {false};
    bool hitch_recovery {false};
};

[[nodiscard]] constexpr std::uint64_t frame_budget_ns(std::uint32_t refresh_hz) noexcept {
    return refresh_hz == 0U ? 0U : 1'000'000'000ULL / refresh_hz;
}

[[nodiscard]] constexpr std::uint64_t saturating_add_ns(
    std::uint64_t a,
    std::uint64_t b) noexcept {
    return b > std::numeric_limits<std::uint64_t>::max() - a
        ? std::numeric_limits<std::uint64_t>::max()
        : a + b;
}

[[nodiscard]] constexpr bool frame_telemetry_valid(const FrameTelemetry& telemetry) noexcept {
    return telemetry.refresh_hz >= 30U && telemetry.refresh_hz <= 240U;
}

[[nodiscard]] constexpr FrameScheduleDecision schedule_frame(
    const FrameTelemetry& telemetry,
    const RenderPressure& pressure) noexcept {
    FrameScheduleDecision decision {
        .maximum_quality = maximum_quality(pressure),
        .preserve_spatial_motion = preserve_spatial_motion(pressure),
        .prioritize_direct_manipulation = telemetry.direct_manipulation_active,
        .hitch_recovery = false,
    };

    if (!frame_telemetry_valid(telemetry)) {
        decision.maximum_quality = QualityTier::essential;
        decision.preserve_spatial_motion = false;
        decision.hitch_recovery = true;
        return decision;
    }

    const auto budget = frame_budget_ns(telemetry.refresh_hz);
    const auto render_work = saturating_add_ns(telemetry.cpu_render_ns, telemetry.gpu_render_ns);
    const auto work = saturating_add_ns(render_work, telemetry.present_wait_ns);

    // Repeated misses mean visual effects are already competing with interaction.
    // Shed optional work before allowing a hitch cluster to grow.
    if (telemetry.consecutive_misses >= 3U || work > budget) {
        decision.maximum_quality = clamp_quality(decision.maximum_quality, QualityTier::continuity);
        decision.hitch_recovery = true;
    }

    // During direct manipulation, input response outranks material/depth effects.
    if (telemetry.direct_manipulation_active) {
        decision.maximum_quality = clamp_quality(decision.maximum_quality, QualityTier::continuity);
    }

    // If the latest input has already waited more than a frame, spatial travel is
    // optional until responsiveness recovers; semantic state changes still occur.
    if (telemetry.input_age_ns > budget) {
        decision.preserve_spatial_motion = false;
        decision.maximum_quality = clamp_quality(decision.maximum_quality, QualityTier::continuity);
    }

    if (decision.maximum_quality == QualityTier::essential) {
        decision.preserve_spatial_motion = false;
    }
    return decision;
}

} // namespace os::ui
