#pragma once

#include <cstdint>

#include <os/ui/render_budget.hpp>
#include <os/ui/renderer.hpp>
#include <os/ui/system_layouts.hpp>
#include <os/ui/system_scenes.hpp>

namespace os::ui {

// Renderer-facing plan for Cookie-owned system scenes. This remains semantic:
// the compositor still owns concrete pixels, shaders, blur kernels and colors.
struct SystemRenderPlan final {
    SystemSceneKind scene {SystemSceneKind::home};
    PlaneRole dominant_plane {PlaneRole::content};
    ContourFamily primary_contour {ContourFamily::anchor};
    ContourFamily accent_contour {ContourFamily::sweep};
    MotionCharacter motion {MotionCharacter::handoff};
    RenderBuildOptions render {};
    bool trusted_attribution_required {false};
    bool capture_protected {false};
    bool wallpaper_context_visible {false};
    bool reserve_display_seam {false};
};

[[nodiscard]] constexpr SystemRenderPlan make_system_render_plan(
    SystemSceneKind scene,
    const DeviceProfile& profile,
    const FrameScheduleDecision& frame,
    bool capture_protected = false) noexcept {
    const auto grammar = scene_grammar(scene, profile);
    const auto budget = render_options_for(frame);

    return SystemRenderPlan{
        .scene = scene,
        .dominant_plane = grammar.dominant_plane,
        .primary_contour = grammar.signature.primary_contour,
        .accent_contour = grammar.signature.accent_contour,
        .motion = grammar.entry_motion,
        .render = budget,
        .trusted_attribution_required = grammar.trusted_attribution_required,
        .capture_protected = capture_protected || scene == SystemSceneKind::lock_screen,
        .wallpaper_context_visible = grammar.wallpaper_context_visible,
        .reserve_display_seam = profile.cutout == CutoutKind::hinge ||
            profile.posture == DevicePosture::unfolded,
    };
}

[[nodiscard]] constexpr bool system_render_plan_valid(
    const SystemRenderPlan& plan) noexcept {
    if (plan.scene == SystemSceneKind::lock_screen &&
        !plan.trusted_attribution_required) {
        return false;
    }
    if (plan.scene == SystemSceneKind::lock_screen && !plan.capture_protected) {
        return false;
    }
    if (plan.dominant_plane == PlaneRole::secure &&
        !plan.trusted_attribution_required) {
        return false;
    }
    if (plan.motion == MotionCharacter::secure &&
        !plan.trusted_attribution_required) {
        return false;
    }
    if (plan.render.quality == VisualQualityTier::economy &&
        plan.render.capabilities.live_backdrop) {
        return false;
    }
    return true;
}

} // namespace os::ui
