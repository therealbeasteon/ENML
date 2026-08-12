#include <cstdlib>

#include <os/ui/device_profile.hpp>
#include <os/ui/frame_scheduler.hpp>
#include <os/ui/home.hpp>
#include <os/ui/icon.hpp>
#include <os/ui/identity.hpp>
#include <os/ui/layout_policy.hpp>
#include <os/ui/motion_stability.hpp>
#include <os/ui/quality_policy.hpp>
#include <os/ui/render_budget.hpp>
#include <os/ui/secure_surface.hpp>

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
    require(device_profile_valid(tall_phone));
    const auto tall_policy = resolve_home_layout_policy(tall_phone);
    require(tall_policy.composition == HomeComposition::reach_biased);
    require(tall_policy.dock_floats_above_safe_inset);
    require(preferred_reach_zone(tall_phone, HomeRegion::dock) == ReachZone::primary);

    const DeviceProfile unfolded {
        .width_q6 = 1536U * 64U,
        .height_q6 = 1840U * 64U,
        .safe_insets = {},
        .posture = DevicePosture::unfolded,
        .width_class = WidthClass::expanded,
        .height_class = HeightClass::regular,
        .cutout = CutoutKind::hinge,
        .rounded_display = false,
        .one_handed_preferred = false,
    };
    require(device_profile_valid(unfolded));
    const auto unfolded_policy = resolve_home_layout_policy(unfolded);
    require(unfolded_policy.composition == HomeComposition::seam_split);
    require(unfolded_policy.reserve_center_seam);

    const DeviceProfile invalid_hinge_slab {
        .width_q6 = 1080U * 64U,
        .height_q6 = 2400U * 64U,
        .safe_insets = {},
        .posture = DevicePosture::slab,
        .width_class = WidthClass::regular,
        .height_class = HeightClass::tall,
        .cutout = CutoutKind::hinge,
        .rounded_display = false,
        .one_handed_preferred = true,
    };
    require(!device_profile_valid(invalid_hinge_slab));

    require(!plane_allowed_for_application(PlaneRole::secure));
    require(plane_allowed_for_application(PlaneRole::content));

    const SecureSurfacePolicy app_secure {
        .plane = PlaneRole::secure,
        .owner = SurfaceOwnerKind::application,
        .trusted_attribution = true,
        .capture = CapturePolicy::denied,
    };
    require(!secure_surface_policy_valid(app_secure));
    require(!application_can_present(app_secure));

    const SecureSurfacePolicy platform_secure {
        .plane = PlaneRole::secure,
        .owner = SurfaceOwnerKind::platform,
        .trusted_attribution = true,
        .capture = CapturePolicy::denied,
    };
    require(secure_surface_policy_valid(platform_secure));

    SecureSurfacePolicy capturable_secure = platform_secure;
    capturable_secure.capture = CapturePolicy::allowed;
    require(!secure_surface_policy_valid(capturable_secure));

    HomeObject private_object {
        .id = HomeObjectId{1U},
        .kind = HomeObjectKind::conversation,
        .preferred_region = HomeRegion::shelf,
        .privacy = HomePrivacyClass::private_metadata,
        .preferred_span_x = 2U,
        .preferred_span_y = 1U,
        .user_pinned = true,
        .remote_enrichment_allowed = false,
    };
    require(home_object_valid(private_object));
    require(!preview_allowed(private_object.privacy, true));
    require(preview_allowed(private_object.privacy, false));
    require(!remote_query_allowed(private_object));

    HomeObject secret_remote = private_object;
    secret_remote.id = HomeObjectId{2U};
    secret_remote.privacy = HomePrivacyClass::secret;
    secret_remote.remote_enrichment_allowed = true;
    require(!home_object_valid(secret_remote));

    IconAssetContract icon {
        .id = IconAssetId{7U},
        .safe_zone_percent = 72U,
        .layer_count = 3U,
        .vector_preferred = true,
        .monochrome_available = true,
        .continuous_animation_requested = false,
    };
    require(icon_contract_valid(icon));

    icon.continuous_animation_requested = true;
    require(!icon_contract_valid(icon));

    const IconDepthLayer safe_depth {
        .role = IconLayerRole::glyph,
        .depth_q8 = 24,
        .parallax_limit_q8 = max_icon_parallax_q8,
        .opacity_percent = 100U,
    };
    require(icon_depth_layer_valid(safe_depth));

    const IconDepthLayer excessive_parallax {
        .role = IconLayerRole::accent,
        .depth_q8 = 32,
        .parallax_limit_q8 = static_cast<std::uint16_t>(max_icon_parallax_q8 + 1U),
        .opacity_percent = 100U,
    };
    require(!icon_depth_layer_valid(excessive_parallax));

    const RenderPressure nominal {};
    require(maximum_quality(nominal) == QualityTier::ambient);
    require(quality_allowed(QualityTier::depth, nominal));
    require(preserve_spatial_motion(nominal));

    const RenderPressure constrained {
        .thermal = PressureLevel::warm,
        .gpu = PressureLevel::constrained,
        .memory = PressureLevel::nominal,
        .battery_saver = false,
        .reduce_motion = false,
        .reduce_transparency = false,
    };
    require(maximum_quality(constrained) == QualityTier::continuity);
    require(!quality_allowed(QualityTier::material, constrained));
    require(quality_allowed(QualityTier::continuity, constrained));

    const RenderPressure critical {
        .thermal = PressureLevel::critical,
        .gpu = PressureLevel::nominal,
        .memory = PressureLevel::nominal,
        .battery_saver = false,
        .reduce_motion = false,
        .reduce_transparency = false,
    };
    require(maximum_quality(critical) == QualityTier::essential);
    require(!quality_allowed(QualityTier::continuity, critical));

    RenderPressure reduced_motion {};
    reduced_motion.reduce_motion = true;
    require(!preserve_spatial_motion(reduced_motion));

    const FrameTelemetry smooth_120hz {
        .refresh_hz = 120U,
        .input_age_ns = 2'000'000ULL,
        .cpu_render_ns = 2'000'000ULL,
        .gpu_render_ns = 2'000'000ULL,
        .present_wait_ns = 1'000'000ULL,
        .consecutive_misses = 0U,
        .direct_manipulation_active = false,
    };
    require(frame_telemetry_valid(smooth_120hz));
    const auto smooth_decision = schedule_frame(smooth_120hz, nominal);
    require(smooth_decision.maximum_quality == QualityTier::ambient);
    require(smooth_decision.preserve_spatial_motion);
    require(!smooth_decision.hitch_recovery);
    const auto smooth_render = render_options_for(smooth_decision);
    require(smooth_render.quality == VisualQualityTier::full);
    require(smooth_render.capabilities.live_backdrop);
    require(smooth_render.capabilities.spatial_motion);

    FrameTelemetry dragging = smooth_120hz;
    dragging.direct_manipulation_active = true;
    const auto drag_decision = schedule_frame(dragging, nominal);
    require(drag_decision.maximum_quality == QualityTier::continuity);
    require(drag_decision.prioritize_direct_manipulation);
    const auto drag_render = render_options_for(drag_decision);
    require(drag_render.quality == VisualQualityTier::economy);
    require(!drag_render.capabilities.live_backdrop);
    require(drag_render.capabilities.max_depth_blur_q6 == 0U);

    FrameTelemetry hitch = smooth_120hz;
    hitch.consecutive_misses = 3U;
    const auto hitch_decision = schedule_frame(hitch, nominal);
    require(hitch_decision.maximum_quality == QualityTier::continuity);
    require(hitch_decision.hitch_recovery);

    FrameTelemetry stale_input = smooth_120hz;
    stale_input.input_age_ns = frame_budget_ns(stale_input.refresh_hz) + 1U;
    const auto stale_decision = schedule_frame(stale_input, nominal);
    require(!stale_decision.preserve_spatial_motion);
    require(stale_decision.maximum_quality == QualityTier::continuity);
    const auto stale_render = render_options_for(stale_decision);
    require(!stale_render.capabilities.spatial_motion);

    const FrameTelemetry invalid_refresh {
        .refresh_hz = 0U,
        .input_age_ns = 0U,
        .cpu_render_ns = 0U,
        .gpu_render_ns = 0U,
        .present_wait_ns = 0U,
        .consecutive_misses = 0U,
        .direct_manipulation_active = false,
    };
    require(!frame_telemetry_valid(invalid_refresh));
    const auto invalid_decision = schedule_frame(invalid_refresh, nominal);
    require(invalid_decision.maximum_quality == QualityTier::essential);
    require(!invalid_decision.preserve_spatial_motion);
    require(invalid_decision.hitch_recovery);
    const auto invalid_render = render_options_for(invalid_decision);
    require(invalid_render.quality == VisualQualityTier::economy);
    require(!invalid_render.capabilities.live_backdrop);
    require(!invalid_render.capabilities.spatial_motion);

    MotionContinuity gesture {
        .progress_q16 = 40'000U,
        .target_q16 = motion_one_q16,
        .direction = MotionDirection::forward,
        .active = true,
    };
    require(motion_continuity_valid(gesture));
    const auto reversed = retarget_motion(gesture, 0U);
    require(reversed.progress_q16 == 40'000U);
    require(reversed.target_q16 == 0U);
    require(reversed.direction == MotionDirection::reverse);
    require(reversed.active);

    const auto resumed = retarget_motion(reversed, motion_one_q16);
    require(resumed.progress_q16 == 40'000U);
    require(resumed.direction == MotionDirection::forward);

    FrameStabilityState stability {
        .admitted_quality = QualityTier::ambient,
        .stable_frames = 0U,
        .miss_streak = 0U,
    };
    stability = update_frame_stability(stability, QualityTier::ambient, true);
    require(stability.admitted_quality == QualityTier::continuity);
    require(stability.miss_streak == 1U);

    for (std::uint8_t i = 0U; i < quality_restore_stable_frames - 1U; ++i) {
        stability = update_frame_stability(stability, QualityTier::ambient, false);
    }
    require(stability.admitted_quality == QualityTier::continuity);
    stability = update_frame_stability(stability, QualityTier::ambient, false);
    require(stability.admitted_quality == QualityTier::material);
    require(stability.stable_frames == 0U);

    return 0;
}
