#pragma once

#include <cstdint>

#include <os/ui/device_profile.hpp>
#include <os/ui/identity.hpp>

namespace os::ui {

// Cookie wallpapers are scene assets, not arbitrary fullscreen bitmaps. The
// renderer owns final crop, contrast treatment, depth and motion admission.
enum class WallpaperSceneKind : std::uint8_t {
    still = 0U,
    layered = 1U,
    procedural = 2U,
};

enum class WallpaperPrivacyClass : std::uint8_t {
    public_ = 0U,
    personal = 1U,
    sensitive = 2U,
};

struct WallpaperAssetContract final {
    WallpaperSceneKind kind {WallpaperSceneKind::still};
    WallpaperPrivacyClass privacy {WallpaperPrivacyClass::public_};
    std::uint32_t source_width_px {0U};
    std::uint32_t source_height_px {0U};
    std::uint8_t layer_count {1U};
    bool hdr_capable {false};
    bool wide_gamut_capable {false};
    bool depth_motion_requested {false};
    bool lock_screen_safe {true};
};

inline constexpr std::uint8_t max_wallpaper_layers = 4U;
inline constexpr std::uint32_t min_wallpaper_long_edge_px = 2'560U;
inline constexpr std::uint32_t preferred_wallpaper_long_edge_px = 3'840U;

[[nodiscard]] constexpr std::uint32_t wallpaper_long_edge(
    const WallpaperAssetContract& asset) noexcept {
    return asset.source_width_px > asset.source_height_px
        ? asset.source_width_px : asset.source_height_px;
}

[[nodiscard]] constexpr bool wallpaper_asset_valid(
    const WallpaperAssetContract& asset) noexcept {
    if (asset.source_width_px == 0U || asset.source_height_px == 0U) return false;
    if (wallpaper_long_edge(asset) < min_wallpaper_long_edge_px) return false;
    if (asset.layer_count == 0U || asset.layer_count > max_wallpaper_layers) return false;
    if (asset.kind == WallpaperSceneKind::still && asset.layer_count != 1U) return false;
    if (asset.kind == WallpaperSceneKind::procedural && asset.source_width_px < 64U) return false;
    if (asset.privacy == WallpaperPrivacyClass::sensitive && asset.lock_screen_safe) return false;
    return true;
}

struct WallpaperPresentationPolicy final {
    QualityTier maximum_quality {QualityTier::ambient};
    bool depth_motion_allowed {true};
    bool reveal_personal_detail {true};
    bool preserve_subject_safe_zone {true};
};

[[nodiscard]] constexpr WallpaperPresentationPolicy resolve_wallpaper_policy(
    const WallpaperAssetContract& asset,
    const DeviceProfile& profile,
    QualityTier available_quality,
    bool device_locked,
    bool reduce_motion) noexcept {
    WallpaperPresentationPolicy policy {
        .maximum_quality = available_quality,
        .depth_motion_allowed = !reduce_motion &&
            asset.depth_motion_requested &&
            available_quality >= QualityTier::depth,
        .reveal_personal_detail = !device_locked ||
            asset.privacy == WallpaperPrivacyClass::public_,
        .preserve_subject_safe_zone = true,
    };

    if (profile.cutout != CutoutKind::none || profile.rounded_display) {
        policy.preserve_subject_safe_zone = true;
    }
    if (device_locked && asset.privacy == WallpaperPrivacyClass::sensitive) {
        policy.maximum_quality = clamp_quality(policy.maximum_quality, QualityTier::continuity);
        policy.depth_motion_allowed = false;
        policy.reveal_personal_detail = false;
    }
    return policy;
}

} // namespace os::ui
