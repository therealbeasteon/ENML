#pragma once

#include <cstdint>

#include <os/ui/device_profile.hpp>
#include <os/ui/system_interactions.hpp>

namespace os::ui {

struct SceneBand final {
    std::uint32_t top_q6 {0U};
    std::uint32_t height_q6 {0U};
};

[[nodiscard]] constexpr bool scene_band_valid(
    const SceneBand& band,
    const DeviceProfile& profile) noexcept {
    if (band.height_q6 == 0U) return false;
    if (band.top_q6 > profile.height_q6) return false;
    return band.height_q6 <= profile.height_q6 - band.top_q6;
}

struct LockLayout final {
    SceneBand trusted_status {};
    SceneBand identity_field {};
    SceneBand notification_field {};
    SceneBand action_field {};
    bool subject_safe_center {true};
};

[[nodiscard]] constexpr LockLayout resolve_lock_layout(
    const DeviceProfile& profile) noexcept {
    const std::uint32_t usable_top = profile.safe_insets.top;
    const std::uint32_t usable_bottom = profile.height_q6 - profile.safe_insets.bottom;
    const std::uint32_t usable = usable_bottom > usable_top ? usable_bottom - usable_top : 0U;
    const std::uint32_t status_h = usable / 10U;
    const std::uint32_t identity_h = usable / 4U;
    const std::uint32_t action_h = usable / 5U;
    const std::uint32_t notification_h = usable > status_h + identity_h + action_h
        ? usable - status_h - identity_h - action_h : 0U;

    return LockLayout{
        .trusted_status = SceneBand{usable_top, status_h},
        .identity_field = SceneBand{usable_top + status_h, identity_h},
        .notification_field = SceneBand{usable_top + status_h + identity_h, notification_h},
        .action_field = SceneBand{usable_bottom - action_h, action_h},
        .subject_safe_center = true,
    };
}

struct QuickControlsLayout final {
    std::uint8_t columns {2U};
    std::uint8_t primary_rows {2U};
    bool lower_weighted {true};
    bool split_around_hinge {false};
};

[[nodiscard]] constexpr QuickControlsLayout resolve_quick_controls_layout(
    const DeviceProfile& profile) noexcept {
    if (profile.posture == DevicePosture::unfolded || profile.cutout == CutoutKind::hinge) {
        return QuickControlsLayout{
            .columns = 4U,
            .primary_rows = 2U,
            .lower_weighted = false,
            .split_around_hinge = true,
        };
    }
    if (profile.width_class == WidthClass::expanded) {
        return QuickControlsLayout{
            .columns = 4U,
            .primary_rows = 2U,
            .lower_weighted = false,
            .split_around_hinge = false,
        };
    }
    if (profile.width_class == WidthClass::regular) {
        return QuickControlsLayout{
            .columns = 3U,
            .primary_rows = 2U,
            .lower_weighted = true,
            .split_around_hinge = false,
        };
    }
    return {};
}

struct NotificationStreamLayout final {
    std::uint8_t visible_groups {3U};
    bool edge_anchored {true};
    bool newest_near_reach_zone {true};
    bool two_column_when_expanded {false};
};

[[nodiscard]] constexpr NotificationStreamLayout resolve_notification_stream_layout(
    const DeviceProfile& profile) noexcept {
    return NotificationStreamLayout{
        .visible_groups = static_cast<std::uint8_t>(
            profile.height_class == HeightClass::tall ? 4U : 3U),
        .edge_anchored = true,
        .newest_near_reach_zone = profile.width_class == WidthClass::compact,
        .two_column_when_expanded = profile.width_class == WidthClass::expanded &&
            profile.cutout != CutoutKind::hinge,
    };
}

struct SwitcherGeometry final {
    AppSwitcherContract contract {};
    std::uint8_t primary_lanes {1U};
    std::uint8_t preview_depth {3U};
    bool reserve_center_seam {false};
    bool selected_task_centered {false};
};

[[nodiscard]] constexpr SwitcherGeometry resolve_switcher_geometry(
    const DeviceProfile& profile) noexcept {
    const AppSwitcherContract contract = resolve_app_switcher_contract(profile);
    switch (contract.layout) {
    case SwitcherLayout::seam_split:
        return SwitcherGeometry{
            .contract = contract,
            .primary_lanes = 2U,
            .preview_depth = 2U,
            .reserve_center_seam = true,
            .selected_task_centered = false,
        };
    case SwitcherLayout::paired_field:
        return SwitcherGeometry{
            .contract = contract,
            .primary_lanes = 2U,
            .preview_depth = 2U,
            .reserve_center_seam = false,
            .selected_task_centered = false,
        };
    case SwitcherLayout::flowing_stack:
        return SwitcherGeometry{
            .contract = contract,
            .primary_lanes = 1U,
            .preview_depth = 3U,
            .reserve_center_seam = false,
            .selected_task_centered = true,
        };
    }
    return {};
}

[[nodiscard]] constexpr bool system_layouts_valid(
    const DeviceProfile& profile) noexcept {
    if (!device_profile_valid(profile)) return false;
    const LockLayout lock = resolve_lock_layout(profile);
    if (!scene_band_valid(lock.trusted_status, profile) ||
        !scene_band_valid(lock.identity_field, profile) ||
        !scene_band_valid(lock.notification_field, profile) ||
        !scene_band_valid(lock.action_field, profile)) {
        return false;
    }
    const QuickControlsLayout controls = resolve_quick_controls_layout(profile);
    if (controls.columns < 2U || controls.columns > 4U) return false;
    const SwitcherGeometry switcher = resolve_switcher_geometry(profile);
    if (switcher.primary_lanes == 0U || switcher.primary_lanes > 2U) return false;
    return true;
}

} // namespace os::ui
