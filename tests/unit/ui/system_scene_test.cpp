#include <cstdlib>

#include <os/ui/frame_scheduler.hpp>
#include <os/ui/quality_policy.hpp>
#include <os/ui/system_composition.hpp>
#include <os/ui/system_layouts.hpp>
#include <os/ui/system_render_plan.hpp>
#include <os/ui/system_scenes.hpp>

namespace {
void require(bool condition) {
    if (!condition) std::abort();
}
} // namespace

int main() {
    using namespace os::ui;

    const DeviceProfile tall_phone {
        .width_q6 = 412U * 64U,
        .height_q6 = 915U * 64U,
        .safe_insets = InsetsQ6{.top = 48U * 64U, .right = 0U, .bottom = 24U * 64U, .left = 0U},
        .posture = DevicePosture::slab,
        .width_class = WidthClass::compact,
        .height_class = HeightClass::tall,
        .cutout = CutoutKind::centered,
        .rounded_display = true,
        .one_handed_preferred = true,
    };

    const DeviceProfile wide_phone {
        .width_q6 = 720U * 64U,
        .height_q6 = 1280U * 64U,
        .safe_insets = InsetsQ6{},
        .posture = DevicePosture::slab,
        .width_class = WidthClass::expanded,
        .height_class = HeightClass::regular,
        .cutout = CutoutKind::none,
        .rounded_display = false,
        .one_handed_preferred = false,
    };

    const DeviceProfile foldable {
        .width_q6 = 1536U * 64U,
        .height_q6 = 1840U * 64U,
        .safe_insets = InsetsQ6{},
        .posture = DevicePosture::unfolded,
        .width_class = WidthClass::expanded,
        .height_class = HeightClass::regular,
        .cutout = CutoutKind::hinge,
        .rounded_display = false,
        .one_handed_preferred = false,
    };

    const auto lock = scene_grammar(SystemSceneKind::lock_screen, tall_phone);
    const auto home = scene_grammar(SystemSceneKind::home, tall_phone);
    const auto controls = scene_grammar(SystemSceneKind::quick_controls, tall_phone);
    const auto notifications = scene_grammar(SystemSceneKind::notifications, tall_phone);
    const auto settings = scene_grammar(SystemSceneKind::settings, tall_phone);
    const auto app = scene_grammar(SystemSceneKind::app_surface, tall_phone);
    const auto switcher = scene_grammar(SystemSceneKind::app_switcher, tall_phone);

    require(system_scene_grammar_valid(lock));
    require(system_scene_grammar_valid(home));
    require(system_scene_grammar_valid(controls));
    require(system_scene_grammar_valid(notifications));
    require(system_scene_grammar_valid(settings));
    require(system_scene_grammar_valid(app));
    require(system_scene_grammar_valid(switcher));

    require(lock.wallpaper_context_visible);
    require(lock.trusted_attribution_required);
    require(lock.lower_reach_priority);

    require(home.signature.primary_contour == ContourFamily::anchor);
    require(home.signature.accent_contour == ContourFamily::sweep);
    require(home.signature.rhythm == SurfaceRhythm::quiet);

    require(controls.dominant_plane == PlaneRole::control);
    require(controls.lower_reach_priority);
    require(controls.allows_dense_content);

    require(notifications.signature.rhythm == SurfaceRhythm::flowing);
    require(settings.signature.rhythm == SurfaceRhythm::grouped);

    require(!app.trusted_attribution_required);
    require(app.dominant_plane == PlaneRole::content);

    require(switcher.entry_motion == MotionCharacter::handoff);
    require(switcher.wallpaper_context_visible);

    const auto wide_home = scene_grammar(SystemSceneKind::home, wide_phone);
    const auto wide_lock = scene_grammar(SystemSceneKind::lock_screen, wide_phone);
    require(system_scene_grammar_valid(wide_home));
    require(system_scene_grammar_valid(wide_lock));
    require(!wide_home.lower_reach_priority);
    require(!wide_lock.lower_reach_priority);

    require(system_layouts_valid(tall_phone));
    const auto lock_layout = resolve_lock_layout(tall_phone);
    require(scene_band_valid(lock_layout.trusted_status, tall_phone));
    require(scene_band_valid(lock_layout.identity_field, tall_phone));
    require(scene_band_valid(lock_layout.notification_field, tall_phone));
    require(scene_band_valid(lock_layout.action_field, tall_phone));
    require(lock_layout.action_field.top_q6 > lock_layout.identity_field.top_q6);

    const auto phone_controls = resolve_quick_controls_layout(tall_phone);
    require(phone_controls.columns == 2U);
    require(phone_controls.lower_weighted);
    require(!phone_controls.split_around_hinge);

    const auto phone_notes = resolve_notification_stream_layout(tall_phone);
    require(phone_notes.visible_groups == 4U);
    require(phone_notes.newest_near_reach_zone);
    require(!phone_notes.two_column_when_expanded);

    const auto phone_switcher = resolve_switcher_geometry(tall_phone);
    require(phone_switcher.contract.layout == SwitcherLayout::flowing_stack);
    require(phone_switcher.primary_lanes == 1U);
    require(phone_switcher.selected_task_centered);

    require(system_layouts_valid(wide_phone));
    const auto wide_controls = resolve_quick_controls_layout(wide_phone);
    require(wide_controls.columns == 4U);
    require(!wide_controls.lower_weighted);
    const auto wide_notes = resolve_notification_stream_layout(wide_phone);
    require(wide_notes.two_column_when_expanded);
    const auto wide_switcher = resolve_switcher_geometry(wide_phone);
    require(wide_switcher.contract.layout == SwitcherLayout::paired_field);
    require(wide_switcher.primary_lanes == 2U);

    require(system_layouts_valid(foldable));
    const auto fold_controls = resolve_quick_controls_layout(foldable);
    require(fold_controls.split_around_hinge);
    const auto fold_switcher = resolve_switcher_geometry(foldable);
    require(fold_switcher.contract.layout == SwitcherLayout::seam_split);
    require(fold_switcher.reserve_center_seam);
    require(!fold_switcher.selected_task_centered);

    const FrameTelemetry smooth_120hz {
        .refresh_hz = 120U,
        .input_age_ns = 1'000'000ULL,
        .cpu_render_ns = 2'000'000ULL,
        .gpu_render_ns = 2'000'000ULL,
        .present_wait_ns = 500'000ULL,
        .consecutive_misses = 0U,
        .direct_manipulation_active = false,
    };
    const RenderPressure nominal {};
    const auto smooth_frame = schedule_frame(smooth_120hz, nominal);
    const auto home_plan = make_system_render_plan(SystemSceneKind::home, tall_phone, smooth_frame);
    require(system_render_plan_valid(home_plan));
    require(home_plan.primary_contour == ContourFamily::anchor);
    require(home_plan.accent_contour == ContourFamily::sweep);
    require(home_plan.render.quality == VisualQualityTier::full);
    require(home_plan.render.capabilities.live_backdrop);

    const auto lock_plan = make_system_render_plan(SystemSceneKind::lock_screen, tall_phone, smooth_frame);
    require(system_render_plan_valid(lock_plan));
    require(lock_plan.trusted_attribution_required);
    require(lock_plan.capture_protected);
    require(lock_plan.wallpaper_context_visible);

    const auto lock_composition = compose_lock_scene(tall_phone, smooth_frame);
    require(system_scene_composition_valid(lock_composition, tall_phone));
    require(lock_composition.count == 4U);
    require(lock_composition.nodes[0].role == SystemRegionRole::trusted_status);
    require(lock_composition.nodes[0].plane == PlaneRole::secure);
    require(lock_composition.nodes[0].trusted);
    require(lock_composition.nodes[0].capture_protected);
    require(lock_composition.nodes[1].contour == ContourFamily::halo);
    require(lock_composition.nodes[1].trusted);
    require(lock_composition.nodes[3].role == SystemRegionRole::reachable_actions);
    require(lock_composition.nodes[3].interactive);

    auto forged_lock = lock_composition;
    forged_lock.nodes[0].trusted = false;
    require(!system_scene_composition_valid(forged_lock, tall_phone));
    forged_lock = lock_composition;
    forged_lock.nodes[1].trusted = false;
    require(!system_scene_composition_valid(forged_lock, tall_phone));

    FrameTelemetry dragging = smooth_120hz;
    dragging.direct_manipulation_active = true;
    const auto drag_frame = schedule_frame(dragging, nominal);
    const auto controls_plan = make_system_render_plan(SystemSceneKind::quick_controls, tall_phone, drag_frame);
    require(system_render_plan_valid(controls_plan));
    require(controls_plan.render.quality == VisualQualityTier::economy);
    require(!controls_plan.render.capabilities.live_backdrop);

    const auto fold_plan = make_system_render_plan(SystemSceneKind::app_switcher, foldable, smooth_frame);
    require(system_render_plan_valid(fold_plan));
    require(fold_plan.reserve_display_seam);
    require(fold_plan.motion == MotionCharacter::handoff);

    const DeviceProfile impossible {};
    require(!system_layouts_valid(impossible));

    return 0;
}
