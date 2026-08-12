#pragma once

#include <cstdint>

namespace os::ui {

enum class IconLayerRole : std::uint8_t {
    background = 0U,
    body = 1U,
    glyph = 2U,
    accent = 3U,
};

enum class IconPresentation : std::uint8_t {
    full_color = 0U,
    dark = 1U,
    monochrome = 2U,
    high_contrast = 3U,
};

struct IconAssetId final {
    std::uint64_t value {0U};

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0U; }
    friend constexpr bool operator==(IconAssetId, IconAssetId) noexcept = default;
};

// Q8 values keep the public asset contract deterministic and inexpensive.
// 256 means 1.0. Apps describe bounded depth intent; they never supply an
// arbitrary shader or continuously-running launcher animation.
struct IconDepthLayer final {
    IconLayerRole role {IconLayerRole::glyph};
    std::int16_t depth_q8 {0};
    std::uint16_t parallax_limit_q8 {0U};
    std::uint8_t opacity_percent {100U};
};

struct IconAssetContract final {
    IconAssetId id {};
    std::uint16_t safe_zone_percent {72U};
    std::uint8_t layer_count {1U};
    bool vector_preferred {true};
    bool monochrome_available {false};
    bool continuous_animation_requested {false};
};

inline constexpr std::uint8_t max_icon_layers = 4U;
inline constexpr std::uint16_t min_icon_safe_zone_percent = 60U;
inline constexpr std::uint16_t max_icon_safe_zone_percent = 88U;
inline constexpr std::uint16_t max_icon_parallax_q8 = 48U;

[[nodiscard]] constexpr bool icon_contract_valid(const IconAssetContract& contract) noexcept {
    if (!contract.id.valid()) return false;
    if (contract.layer_count == 0U || contract.layer_count > max_icon_layers) return false;
    if (contract.safe_zone_percent < min_icon_safe_zone_percent ||
        contract.safe_zone_percent > max_icon_safe_zone_percent) return false;
    // Cookie Home does not grant apps a permanent animation clock.
    if (contract.continuous_animation_requested) return false;
    return true;
}

[[nodiscard]] constexpr bool icon_depth_layer_valid(const IconDepthLayer& layer) noexcept {
    return layer.opacity_percent <= 100U && layer.parallax_limit_q8 <= max_icon_parallax_q8;
}

[[nodiscard]] constexpr bool presentation_requires_monochrome(
    IconPresentation presentation) noexcept {
    return presentation == IconPresentation::monochrome ||
           presentation == IconPresentation::high_contrast;
}

} // namespace os::ui
