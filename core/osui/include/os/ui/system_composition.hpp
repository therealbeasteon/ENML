#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/ui/system_layouts.hpp>
#include <os/ui/system_render_plan.hpp>

namespace os::ui {

enum class SystemRegionRole : std::uint8_t {
    trusted_status = 0U,
    identity = 1U,
    primary_content = 2U,
    secondary_content = 3U,
    reachable_actions = 4U,
    seam_guard = 5U,
};

struct SystemRegionNode final {
    SystemRegionRole role {SystemRegionRole::primary_content};
    PlaneRole plane {PlaneRole::content};
    ContourFamily contour {ContourFamily::anchor};
    SceneBand vertical {};
    bool trusted {false};
    bool capture_protected {false};
    bool interactive {false};
};

struct SystemSceneComposition final {
    SystemRenderPlan plan {};
    std::array<SystemRegionNode, 8U> nodes {};
    std::size_t count {0U};
    std::uint8_t columns {1U};
    std::uint8_t lanes {1U};
    bool lower_weighted {false};
    bool newest_near_reach_zone {false};
    bool split_around_hinge {false};
};

[[nodiscard]] constexpr bool append_region(
    SystemSceneComposition& composition,
    const SystemRegionNode& node) noexcept {
    if (composition.count >= composition.nodes.size()) return false;
    composition.nodes[composition.count++] = node;
    return true;
}

[[nodiscard]] constexpr SceneBand usable_scene_band(
    const DeviceProfile& profile) noexcept {
    const std::uint32_t top = profile.safe_insets.top;
    const std::uint32_t bottom = profile.height_q6 >= profile.safe_insets.bottom
        ? profile.height_q6 - profile.safe_insets.bottom : 0U;
    return SceneBand{top, bottom > top ? bottom - top : 0U};
}

[[nodiscard]] constexpr SystemSceneComposition compose_lock_scene(
    const DeviceProfile& profile,
    const FrameScheduleDecision& frame) noexcept {
    SystemSceneComposition composition {};
    composition.plan = make_system_render_plan(
        SystemSceneKind::lock_screen, profile, frame, true);
    const auto layout = resolve_lock_layout(profile);

    append_region(composition, SystemRegionNode{
        .role = SystemRegionRole::trusted_status,
        .plane = PlaneRole::secure,
        .contour = ContourFamily::frame,
        .vertical = layout.trusted_status,
        .trusted = true,
        .capture_protected = true,
        .interactive = false,
    });
    append_region(composition, SystemRegionNode{
        .role = SystemRegionRole::identity,
        .plane = PlaneRole::content,
        .contour = ContourFamily::halo,
        .vertical = layout.identity_field,
        .trusted = true,
        .capture_protected = true,
        .interactive = false,
    });
    append_region(composition, SystemRegionNode{
        .role = SystemRegionRole::primary_content,
        .plane = PlaneRole::content,
        .contour = ContourFamily::sweep,
        .vertical = layout.notification_field,
        .trusted = false,
        .capture_protected = true,
        .interactive = true,
    });
    append_region(composition, SystemRegionNode{
        .role = SystemRegionRole::reachable_actions,
        .plane = PlaneRole::control,
        .contour = ContourFamily::pebble,
        .vertical = layout.action_field,
        .trusted = true,
        .capture_protected = true,
        .interactive = true,
    });
    return composition;
}

[[nodiscard]] constexpr SystemSceneComposition compose_quick_controls_scene(
    const DeviceProfile& profile,
    const FrameScheduleDecision& frame) noexcept {
    SystemSceneComposition composition {};
    composition.plan = make_system_render_plan(
        SystemSceneKind::quick_controls, profile, frame);
    const auto layout = resolve_quick_controls_layout(profile);
    const auto usable = usable_scene_band(profile);
    const std::uint32_t context_h = layout.lower_weighted ? usable.height_q6 / 3U : usable.height_q6 / 4U;
    const SceneBand context{usable.top_q6, context_h};
    const SceneBand controls{
        usable.top_q6 + context_h,
        usable.height_q6 >= context_h ? usable.height_q6 - context_h : 0U};

    composition.columns = layout.columns;
    composition.lanes = layout.split_around_hinge ? 2U : 1U;
    composition.lower_weighted = layout.lower_weighted;
    composition.split_around_hinge = layout.split_around_hinge;

    append_region(composition, SystemRegionNode{
        .role = SystemRegionRole::secondary_content,
        .plane = PlaneRole::content,
        .contour = ContourFamily::sweep,
        .vertical = context,
        .trusted = false,
        .capture_protected = false,
        .interactive = false,
    });
    append_region(composition, SystemRegionNode{
        .role = SystemRegionRole::reachable_actions,
        .plane = PlaneRole::control,
        .contour = ContourFamily::pebble,
        .vertical = controls,
        .trusted = false,
        .capture_protected = false,
        .interactive = true,
    });
    return composition;
}

[[nodiscard]] constexpr SystemSceneComposition compose_notification_scene(
    const DeviceProfile& profile,
    const FrameScheduleDecision& frame,
    bool lock_context = false) noexcept {
    SystemSceneComposition composition {};
    composition.plan = make_system_render_plan(
        SystemSceneKind::notifications, profile, frame, lock_context);
    const auto layout = resolve_notification_stream_layout(profile);
    const auto usable = usable_scene_band(profile);
    const std::uint32_t lead_h = usable.height_q6 / 5U;
    const SceneBand lead{usable.top_q6, lead_h};
    const SceneBand stream{
        usable.top_q6 + lead_h,
        usable.height_q6 >= lead_h ? usable.height_q6 - lead_h : 0U};

    composition.columns = layout.two_column_when_expanded ? 2U : 1U;
    composition.lanes = composition.columns;
    composition.newest_near_reach_zone = layout.newest_near_reach_zone;
    composition.split_around_hinge = profile.cutout == CutoutKind::hinge;

    append_region(composition, SystemRegionNode{
        .role = SystemRegionRole::secondary_content,
        .plane = PlaneRole::content,
        .contour = ContourFamily::anchor,
        .vertical = lead,
        .trusted = false,
        .capture_protected = lock_context,
        .interactive = false,
    });
    append_region(composition, SystemRegionNode{
        .role = SystemRegionRole::primary_content,
        .plane = PlaneRole::content,
        .contour = ContourFamily::sweep,
        .vertical = stream,
        .trusted = false,
        .capture_protected = lock_context,
        .interactive = true,
    });
    return composition;
}

[[nodiscard]] constexpr bool system_scene_composition_valid(
    const SystemSceneComposition& composition,
    const DeviceProfile& profile) noexcept {
    if (!system_render_plan_valid(composition.plan)) return false;
    if (composition.count == 0U || composition.count > composition.nodes.size()) return false;
    if (composition.columns == 0U || composition.columns > 4U) return false;
    if (composition.lanes == 0U || composition.lanes > 2U) return false;
    if (composition.split_around_hinge && profile.cutout != CutoutKind::hinge &&
        profile.posture != DevicePosture::unfolded) return false;
    for (std::size_t i = 0U; i < composition.count; ++i) {
        const auto& node = composition.nodes[i];
        if (!scene_band_valid(node.vertical, profile)) return false;
        if (node.plane == PlaneRole::secure && (!node.trusted || !node.capture_protected)) {
            return false;
        }
        if (node.contour == ContourFamily::halo && !node.trusted) return false;
    }
    return true;
}

} // namespace os::ui
