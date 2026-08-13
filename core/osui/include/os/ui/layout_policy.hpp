#pragma once

#include <cstdint>

#include <os/ui/device_profile.hpp>
#include <os/ui/home.hpp>
#include <os/ui/identity.hpp>

namespace os::ui {

enum class ReachZone : std::uint8_t {
    primary = 0U,
    secondary = 1U,
    distant = 2U,
};

enum class HomeComposition : std::uint8_t {
    single_field = 0U,
    reach_biased = 1U,
    dual_pane = 2U,
    seam_split = 3U,
};

struct HomeLayoutPolicy final {
    HomeComposition composition {HomeComposition::single_field};
    ReachZone frequent_action_zone {ReachZone::primary};
    bool reserve_center_seam {false};
    bool now_compact {false};
    bool index_edge_reachable {true};
};

[[nodiscard]] constexpr ReachZone preferred_reach_zone(
    const DeviceProfile& profile,
    HomeRegion region) noexcept {
    // Index is the stable discovery entrance and remains reachable without a
    // permanent dock. Now follows reach on tall phones; Pinned preserves the
    // user's learned spatial map instead of being dragged to the thumb.
    if (region == HomeRegion::index) return ReachZone::primary;
    if (region == HomeRegion::now && prefer_bottom_reach_controls(profile)) {
        return ReachZone::primary;
    }
    if (profile.width_class == WidthClass::expanded) return ReachZone::secondary;
    return region == HomeRegion::pinned ? ReachZone::secondary : ReachZone::primary;
}

[[nodiscard]] constexpr HomeLayoutPolicy resolve_home_layout_policy(
    const DeviceProfile& profile) noexcept {
    if (!device_profile_valid(profile)) return {};

    HomeLayoutPolicy policy {};
    policy.frequent_action_zone = ReachZone::primary;
    policy.reserve_center_seam = avoid_center_seam(profile);
    policy.now_compact = profile.height_class == HeightClass::short_;
    policy.index_edge_reachable = true;

    if (profile.cutout == CutoutKind::hinge || profile.posture == DevicePosture::half_open) {
        policy.composition = HomeComposition::seam_split;
    } else if (should_recompose_two_pane(profile)) {
        policy.composition = HomeComposition::dual_pane;
    } else if (prefer_bottom_reach_controls(profile)) {
        policy.composition = HomeComposition::reach_biased;
    } else {
        policy.composition = HomeComposition::single_field;
    }
    return policy;
}

[[nodiscard]] constexpr bool plane_allowed_for_application(PlaneRole plane) noexcept {
    return !platform_owned_plane(plane);
}

} // namespace os::ui
