#include <cstdlib>

#include <os/ui/device_profile.hpp>
#include <os/ui/home.hpp>
#include <os/ui/icon.hpp>
#include <os/ui/identity.hpp>
#include <os/ui/layout_policy.hpp>

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

    return 0;
}
