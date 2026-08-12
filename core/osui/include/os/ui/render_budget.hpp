#pragma once

#include <os/ui/frame_scheduler.hpp>
#include <os/ui/renderer.hpp>

namespace os::ui {

[[nodiscard]] constexpr VisualQualityTier renderer_quality_for(QualityTier quality) noexcept {
    switch (quality) {
    case QualityTier::essential:
    case QualityTier::continuity:
        return VisualQualityTier::economy;
    case QualityTier::material:
    case QualityTier::depth:
        return VisualQualityTier::balanced;
    case QualityTier::ambient:
        return VisualQualityTier::full;
    }
    return VisualQualityTier::economy;
}

[[nodiscard]] constexpr RenderCapabilities capabilities_for(
    QualityTier quality,
    RenderCapabilities hardware = {}) noexcept {
    RenderCapabilities resolved = hardware;

    if (quality <= QualityTier::continuity) {
        resolved.live_backdrop = false;
        resolved.max_backdrop_blur_q6 = 0U;
        resolved.max_depth_blur_q6 = 0U;
    } else if (quality == QualityTier::material) {
        resolved.max_depth_blur_q6 = 0U;
    }

    if (quality == QualityTier::essential) {
        resolved.spatial_motion = false;
    }
    return resolved;
}

[[nodiscard]] constexpr RenderBuildOptions render_options_for(
    const FrameScheduleDecision& decision,
    RenderBuildOptions base = {}) noexcept {
    base.quality = renderer_quality_for(decision.maximum_quality);
    base.capabilities = capabilities_for(decision.maximum_quality, base.capabilities);
    if (!decision.preserve_spatial_motion) {
        base.capabilities.spatial_motion = false;
    }
    return base;
}

} // namespace os::ui
