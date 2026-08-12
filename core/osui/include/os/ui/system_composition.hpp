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
};

[[nodiscard]] constexpr bool append_region(
    SystemSceneComposition& composition,
    const SystemRegionNode& node) noexcept {
    if (composition.count >= composition.nodes.size()) return false;
    composition.nodes[composition.count++] = node;
    return true;
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

[[nodiscard]] constexpr bool system_scene_composition_valid(
    const SystemSceneComposition& composition,
    const DeviceProfile& profile) noexcept {
    if (!system_render_plan_valid(composition.plan)) return false;
    if (composition.count == 0U || composition.count > composition.nodes.size()) return false;
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
