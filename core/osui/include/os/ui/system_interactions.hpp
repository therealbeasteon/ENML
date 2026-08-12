#pragma once

#include <cstdint>

#include <os/ui/device_profile.hpp>
#include <os/ui/home.hpp>
#include <os/ui/identity.hpp>
#include <os/ui/system_scenes.hpp>

namespace os::ui {

enum class NavigationMode : std::uint8_t {
    gesture = 0U,
    compact_controls = 1U,
    keyboard_focus = 2U,
    assistive = 3U,
};

struct NavigationContract final {
    NavigationMode mode {NavigationMode::gesture};
    bool back_available {false};
    bool home_available {true};
    bool switcher_available {true};
    bool lower_reach_bias {true};
    bool persistent_bar_required {false};
};

[[nodiscard]] constexpr NavigationContract resolve_navigation_contract(
    InputMode input,
    const DeviceProfile& profile,
    bool can_go_back) noexcept {
    NavigationContract contract {};
    contract.back_available = can_go_back;
    contract.lower_reach_bias = profile.width_class == WidthClass::compact;

    switch (input) {
    case InputMode::touch:
    case InputMode::stylus:
        contract.mode = NavigationMode::gesture;
        contract.persistent_bar_required = false;
        break;
    case InputMode::pointer:
        contract.mode = NavigationMode::compact_controls;
        contract.persistent_bar_required = false;
        break;
    case InputMode::keyboard:
        contract.mode = NavigationMode::keyboard_focus;
        contract.persistent_bar_required = false;
        break;
    case InputMode::switch_access:
    case InputMode::assistive:
        contract.mode = NavigationMode::assistive;
        contract.persistent_bar_required = true;
        break;
    }
    return contract;
}

enum class QuickControlKind : std::uint8_t {
    toggle = 0U,
    range = 1U,
    route = 2U,
    privacy = 3U,
    trusted_action = 4U,
};

struct QuickControlContract final {
    QuickControlKind kind {QuickControlKind::toggle};
    std::uint8_t priority {0U};
    bool available_while_locked {false};
    bool reveals_private_state {false};
    bool trusted_attribution {false};
    bool destructive {false};
};

[[nodiscard]] constexpr bool quick_control_valid(
    const QuickControlContract& control) noexcept {
    if (control.priority > 7U) return false;
    if (control.available_while_locked && control.reveals_private_state) return false;
    if (control.kind == QuickControlKind::privacy && !control.trusted_attribution) return false;
    if (control.kind == QuickControlKind::trusted_action && !control.trusted_attribution) return false;
    if (control.destructive && control.available_while_locked) return false;
    return true;
}

enum class NotificationTrust : std::uint8_t {
    application = 0U,
    system = 1U,
    privacy = 2U,
    security = 3U,
};

struct NotificationContract final {
    std::uint64_t group_id {0U};
    NotificationTrust trust {NotificationTrust::application};
    HomePrivacyClass privacy {HomePrivacyClass::public_};
    std::uint8_t action_count {0U};
    bool heads_up_requested {false};
    bool ongoing {false};
    bool remote_content_requested {false};
};

inline constexpr std::uint8_t max_notification_actions = 3U;

[[nodiscard]] constexpr bool notification_contract_valid(
    const NotificationContract& note) noexcept {
    if (note.group_id == 0U) return false;
    if (note.action_count > max_notification_actions) return false;
    if (note.privacy == HomePrivacyClass::secret && note.remote_content_requested) return false;
    if (note.trust == NotificationTrust::security && note.heads_up_requested) return false;
    return true;
}

struct NotificationPresentation final {
    bool show_content {true};
    bool allow_actions {true};
    bool allow_remote_content {true};
    bool emphasize_trust {false};
};

[[nodiscard]] constexpr NotificationPresentation resolve_notification_presentation(
    const NotificationContract& note,
    bool device_locked) noexcept {
    NotificationPresentation p {
        .show_content = true,
        .allow_actions = true,
        .allow_remote_content = note.remote_content_requested,
        .emphasize_trust = note.trust != NotificationTrust::application,
    };
    if (device_locked && note.privacy != HomePrivacyClass::public_) {
        p.show_content = false;
        p.allow_actions = false;
        p.allow_remote_content = false;
    }
    if (note.privacy == HomePrivacyClass::secret) {
        p.allow_remote_content = false;
    }
    return p;
}

struct LockSceneContract final {
    bool wallpaper_visible {true};
    bool private_content_visible {false};
    bool quick_controls_available {true};
    bool trusted_status_visible {true};
    bool lower_reach_actions {true};
};

[[nodiscard]] constexpr LockSceneContract resolve_lock_scene_contract(
    const DeviceProfile& profile,
    bool owner_authenticated) noexcept {
    return LockSceneContract{
        .wallpaper_visible = true,
        .private_content_visible = owner_authenticated,
        .quick_controls_available = true,
        .trusted_status_visible = true,
        .lower_reach_actions = profile.width_class == WidthClass::compact,
    };
}

enum class SwitcherLayout : std::uint8_t {
    flowing_stack = 0U,
    paired_field = 1U,
    seam_split = 2U,
};

struct AppSwitcherContract final {
    SwitcherLayout layout {SwitcherLayout::flowing_stack};
    std::uint8_t max_visible_tasks {3U};
    bool preserves_spatial_order {true};
    bool direct_dismiss_allowed {true};
    bool wallpaper_context_visible {true};
};

[[nodiscard]] constexpr AppSwitcherContract resolve_app_switcher_contract(
    const DeviceProfile& profile) noexcept {
    if (profile.posture == DevicePosture::unfolded || profile.cutout == CutoutKind::hinge) {
        return AppSwitcherContract{
            .layout = SwitcherLayout::seam_split,
            .max_visible_tasks = 4U,
            .preserves_spatial_order = true,
            .direct_dismiss_allowed = true,
            .wallpaper_context_visible = true,
        };
    }
    if (profile.width_class == WidthClass::expanded) {
        return AppSwitcherContract{
            .layout = SwitcherLayout::paired_field,
            .max_visible_tasks = 4U,
            .preserves_spatial_order = true,
            .direct_dismiss_allowed = true,
            .wallpaper_context_visible = true,
        };
    }
    return {};
}

} // namespace os::ui
