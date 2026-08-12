#include <cstdlib>

#include <os/ui/device_profile.hpp>
#include <os/ui/wallpaper.hpp>
#include <os/ui/widget.hpp>

namespace {
void require(bool condition) {
    if (!condition) std::abort();
}
} // namespace

int main() {
    using namespace os::ui;

    const DeviceProfile phone {
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

    WallpaperAssetContract hd_scene {
        .kind = WallpaperSceneKind::layered,
        .privacy = WallpaperPrivacyClass::public_,
        .source_width_px = 2'160U,
        .source_height_px = 3'840U,
        .layer_count = 3U,
        .hdr_capable = true,
        .wide_gamut_capable = true,
        .depth_motion_requested = true,
        .lock_screen_safe = true,
    };
    require(wallpaper_asset_valid(hd_scene));
    require(wallpaper_long_edge(hd_scene) == preferred_wallpaper_long_edge_px);

    const auto rich_wallpaper = resolve_wallpaper_policy(
        hd_scene, phone, QualityTier::ambient, false, false);
    require(rich_wallpaper.depth_motion_allowed);
    require(rich_wallpaper.reveal_personal_detail);

    WallpaperAssetContract sensitive = hd_scene;
    sensitive.privacy = WallpaperPrivacyClass::sensitive;
    sensitive.lock_screen_safe = false;
    require(wallpaper_asset_valid(sensitive));
    const auto locked_sensitive = resolve_wallpaper_policy(
        sensitive, phone, QualityTier::ambient, true, false);
    require(!locked_sensitive.depth_motion_allowed);
    require(!locked_sensitive.reveal_personal_detail);
    require(locked_sensitive.maximum_quality == QualityTier::continuity);

    WallpaperAssetContract low_resolution = hd_scene;
    low_resolution.source_width_px = 720U;
    low_resolution.source_height_px = 1'280U;
    require(!wallpaper_asset_valid(low_resolution));

    LivingTileContract weather {
        .kind = LivingTileKind::information,
        .size = LivingTileSize::wide,
        .privacy = HomePrivacyClass::public_,
        .freshness = TileFreshnessClass::event_driven,
        .action_count = 2U,
        .content_items = 6U,
        .resizeable = true,
        .remote_refresh_requested = true,
        .continuous_animation_requested = false,
        .trusted_attribution = false,
    };
    require(living_tile_valid(weather));
    const auto weather_presentation = resolve_living_tile_presentation(
        weather, false, QualityTier::ambient, false);
    require(weather_presentation.show_content);
    require(weather_presentation.allow_remote_refresh);
    require(weather_presentation.allow_motion);

    LivingTileContract private_messages = weather;
    private_messages.kind = LivingTileKind::collection;
    private_messages.privacy = HomePrivacyClass::private_metadata;
    const auto locked_messages = resolve_living_tile_presentation(
        private_messages, true, QualityTier::continuity, false);
    require(!locked_messages.show_content);
    require(!locked_messages.allow_remote_refresh);

    LivingTileContract unsafe_secret = private_messages;
    unsafe_secret.privacy = HomePrivacyClass::secret;
    unsafe_secret.remote_refresh_requested = true;
    require(!living_tile_valid(unsafe_secret));

    LivingTileContract animated = weather;
    animated.continuous_animation_requested = true;
    require(!living_tile_valid(animated));

    LivingTileContract forged_system = weather;
    forged_system.kind = LivingTileKind::trusted_system;
    forged_system.trusted_attribution = false;
    require(!living_tile_valid(forged_system));

    return 0;
}
