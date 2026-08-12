#include <cstdlib>

#include <os/ui/system_interactions.hpp>

namespace {
void require(bool condition) { if (!condition) std::abort(); }
}

int main() {
    using namespace os::ui;

    const DeviceProfile tall_phone {
        .width_q6 = 412U * 64U,
        .height_q6 = 915U * 64U,
        .safe_insets = {},
        .posture = DevicePosture::slab,
        .width_class = WidthClass::compact,
        .height_class = HeightClass::tall,
        .cutout = CutoutKind::none,
        .rounded_display = true,
        .one_handed_preferred = true,
    };

    const auto touch_nav = resolve_navigation_contract(InputMode::touch, tall_phone, true);
    require(touch_nav.mode == NavigationMode::gesture);
    require(touch_nav.back_available);
    require(touch_nav.lower_reach_bias);
    require(!touch_nav.persistent_bar_required);

    const auto assistive_nav = resolve_navigation_contract(InputMode::assistive, tall_phone, true);
    require(assistive_nav.mode == NavigationMode::assistive);
    require(assistive_nav.persistent_bar_required);

    const QuickControlContract safe_toggle {
        .kind = QuickControlKind::toggle,
        .priority = 2U,
        .available_while_locked = true,
        .reveals_private_state = false,
        .trusted_attribution = false,
        .destructive = false,
    };
    require(quick_control_valid(safe_toggle));

    auto leaking_locked = safe_toggle;
    leaking_locked.reveals_private_state = true;
    require(!quick_control_valid(leaking_locked));

    const QuickControlContract forged_privacy {
        .kind = QuickControlKind::privacy,
        .priority = 1U,
        .available_while_locked = false,
        .reveals_private_state = false,
        .trusted_attribution = false,
        .destructive = false,
    };
    require(!quick_control_valid(forged_privacy));

    const NotificationContract private_note {
        .group_id = 7U,
        .trust = NotificationTrust::application,
        .privacy = HomePrivacyClass::private_metadata,
        .action_count = 2U,
        .heads_up_requested = true,
        .ongoing = false,
        .remote_content_requested = true,
    };
    require(notification_contract_valid(private_note));
    const auto locked_note = resolve_notification_presentation(private_note, true);
    require(!locked_note.show_content);
    require(!locked_note.allow_actions);
    require(!locked_note.allow_remote_content);

    auto secret_remote = private_note;
    secret_remote.privacy = HomePrivacyClass::secret;
    require(!notification_contract_valid(secret_remote));

    NotificationContract security_note = private_note;
    security_note.privacy = HomePrivacyClass::public_;
    security_note.trust = NotificationTrust::security;
    security_note.heads_up_requested = true;
    security_note.remote_content_requested = false;
    require(!notification_contract_valid(security_note));

    const auto locked = resolve_lock_scene_contract(tall_phone, false);
    require(locked.wallpaper_visible);
    require(!locked.private_content_visible);
    require(locked.trusted_status_visible);
    require(locked.lower_reach_actions);

    const auto unlocked = resolve_lock_scene_contract(tall_phone, true);
    require(unlocked.private_content_visible);

    const auto phone_switcher = resolve_app_switcher_contract(tall_phone);
    require(phone_switcher.layout == SwitcherLayout::flowing_stack);
    require(phone_switcher.preserves_spatial_order);

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
    const auto fold_switcher = resolve_app_switcher_contract(unfolded);
    require(fold_switcher.layout == SwitcherLayout::seam_split);
    require(fold_switcher.max_visible_tasks == 4U);
    require(fold_switcher.preserves_spatial_order);

    return 0;
}
