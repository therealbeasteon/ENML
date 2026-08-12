#pragma once

#include <cstdint>

#include <os/ui/home.hpp>
#include <os/ui/identity.hpp>

namespace os::ui {

// Cookie calls its widget surface a Living Tile. This is semantic platform
// vocabulary; it does not imply Windows Live Tile geometry or behavior.
enum class LivingTileKind : std::uint8_t {
    information = 0U,
    collection = 1U,
    control = 2U,
    media = 3U,
    trusted_system = 4U,
};

enum class LivingTileSize : std::uint8_t {
    compact = 0U,
    standard = 1U,
    wide = 2U,
    tall = 3U,
    expanded = 4U,
};

enum class TileFreshnessClass : std::uint8_t {
    event_driven = 0U,
    relaxed = 1U,
    periodic = 2U,
};

struct LivingTileContract final {
    LivingTileKind kind {LivingTileKind::information};
    LivingTileSize size {LivingTileSize::standard};
    HomePrivacyClass privacy {HomePrivacyClass::public_};
    TileFreshnessClass freshness {TileFreshnessClass::event_driven};
    std::uint8_t action_count {0U};
    std::uint8_t content_items {1U};
    bool resizeable {true};
    bool remote_refresh_requested {false};
    bool continuous_animation_requested {false};
    bool trusted_attribution {false};
};

inline constexpr std::uint8_t max_tile_actions = 4U;
inline constexpr std::uint8_t max_tile_content_items = 12U;

[[nodiscard]] constexpr bool living_tile_valid(
    const LivingTileContract& tile) noexcept {
    if (tile.action_count > max_tile_actions) return false;
    if (tile.content_items == 0U || tile.content_items > max_tile_content_items) return false;
    if (tile.continuous_animation_requested) return false;
    if (tile.kind == LivingTileKind::trusted_system && !tile.trusted_attribution) return false;
    if (tile.kind != LivingTileKind::trusted_system && tile.trusted_attribution) return false;
    if (tile.privacy == HomePrivacyClass::secret && tile.remote_refresh_requested) return false;
    if (tile.freshness == TileFreshnessClass::periodic &&
        tile.kind == LivingTileKind::control) return false;
    return true;
}

struct LivingTilePresentation final {
    QualityTier maximum_quality {QualityTier::ambient};
    bool show_content {true};
    bool allow_remote_refresh {true};
    bool allow_motion {true};
};

[[nodiscard]] constexpr LivingTilePresentation resolve_living_tile_presentation(
    const LivingTileContract& tile,
    bool device_locked,
    QualityTier available_quality,
    bool reduce_motion) noexcept {
    LivingTilePresentation presentation {
        .maximum_quality = available_quality,
        .show_content = true,
        .allow_remote_refresh = tile.remote_refresh_requested,
        .allow_motion = !reduce_motion && available_quality >= QualityTier::continuity,
    };

    if (device_locked && tile.privacy != HomePrivacyClass::public_) {
        presentation.show_content = false;
        presentation.allow_remote_refresh = false;
    }
    if (tile.privacy == HomePrivacyClass::secret) {
        presentation.allow_remote_refresh = false;
    }
    if (available_quality == QualityTier::essential) {
        presentation.allow_motion = false;
    }
    return presentation;
}

} // namespace os::ui
