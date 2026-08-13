#pragma once

#include <cstdint>

#include <os/ui/mobile_discipline.hpp>
#include <os/ui/system_composition.hpp>

namespace os::ui {

struct SwitcherPreviewComposition final {
    SystemSceneComposition scene {};
    std::uint8_t visible_previews {0U};
    std::uint8_t preview_depth {0U};
    bool selected_task_centered {false};
    bool preserve_task_order {true};
    TaskReturnMode return_mode {TaskReturnMode::live_preview};
};

[[nodiscard]] constexpr SwitcherPreviewComposition compose_switcher_scene(
    const DeviceProfile& profile,
    const FrameScheduleDecision& frame,
    bool memory_pressure = false) noexcept {
    SwitcherPreviewComposition result {};
    result.scene.plan = make_system_render_plan(SystemSceneKind::app_switcher, profile, frame);

    const auto geometry = resolve_switcher_geometry(profile);
    const auto usable = usable_scene_band(profile);
    result.scene.columns = geometry.primary_lanes;
    result.scene.lanes = geometry.primary_lanes;
    result.scene.split_around_hinge = geometry.reserve_center_seam;
    result.visible_previews = geometry.preview_depth;
    result.preview_depth = geometry.preview_depth;
    result.selected_task_centered = geometry.selected_task_centered;
    result.return_mode = preferred_task_return(memory_pressure);

    // Under pressure, keep spatial order and handoff semantics but avoid
    // requiring live task surfaces to remain resident.
    if (result.return_mode == TaskReturnMode::spatial_handoff && result.visible_previews > 1U) {
        result.visible_previews = 1U;
        result.preview_depth = 1U;
    }

    if (geometry.reserve_center_seam) {
        const std::uint32_t guard_h = usable.height_q6 / 12U;
        append_region(result.scene, SystemRegionNode{
            .role = SystemRegionRole::seam_guard,
            .plane = PlaneRole::background,
            .contour = ContourFamily::frame,
            .vertical = SceneBand{usable.top_q6 + (usable.height_q6 - guard_h) / 2U, guard_h},
            .trusted = true,
            .capture_protected = false,
            .interactive = false,
        });
    }

    append_region(result.scene, SystemRegionNode{
        .role = SystemRegionRole::primary_content,
        .plane = PlaneRole::transient,
        .contour = ContourFamily::anchor,
        .vertical = usable,
        .trusted = false,
        .capture_protected = false,
        .interactive = true,
    });
    return result;
}

[[nodiscard]] constexpr bool switcher_preview_composition_valid(
    const SwitcherPreviewComposition& composition,
    const DeviceProfile& profile) noexcept {
    if (!system_scene_composition_valid(composition.scene, profile)) return false;
    if (composition.scene.plan.scene != SystemSceneKind::app_switcher) return false;
    if (composition.visible_previews == 0U || composition.visible_previews > 3U) return false;
    if (composition.preview_depth != composition.visible_previews) return false;
    if (!composition.preserve_task_order) return false;
    if (composition.scene.split_around_hinge && composition.selected_task_centered) return false;
    if (composition.return_mode == TaskReturnMode::spatial_handoff && composition.visible_previews != 1U) return false;
    return true;
}

} // namespace os::ui
