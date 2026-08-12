#pragma once

#include <cstdint>

#include <os/ui/identity.hpp>

namespace os::ui {

// Cookie's identity must survive grayscale, reduced transparency, static
// wallpapers and economy rendering. These roles encode composition rather than
// paint so recognizability does not depend on blur, color or vendor effects.
enum class SpatialBias : std::uint8_t {
    centered = 0U,
    lower_weighted = 1U,
    edge_anchored = 2U,
    asymmetric_balanced = 3U,
};

enum class SurfaceRhythm : std::uint8_t {
    quiet = 0U,
    grouped = 1U,
    flowing = 2U,
    focused = 3U,
};

enum class MotionCharacter : std::uint8_t {
    settle = 0U,      // short positional settling after direct manipulation
    reveal = 1U,      // content emerges from an existing spatial relationship
    handoff = 2U,     // continuity between surfaces/objects
    confirm = 3U,     // restrained completion acknowledgement
    secure = 4U,      // deterministic, low-amplitude trusted transition
};

// SignatureProfile is a renderer/layout policy contract, not app styling ABI.
// It defines how Cookie combines its semantic planes and contours.
struct SignatureProfile final {
    SpatialBias bias {SpatialBias::asymmetric_balanced};
    SurfaceRhythm rhythm {SurfaceRhythm::quiet};
    ContourFamily primary_contour {ContourFamily::anchor};
    ContourFamily accent_contour {ContourFamily::sweep};
    MotionCharacter motion {MotionCharacter::settle};
    std::uint8_t dominant_surface_count {1U};
    std::uint8_t accent_surface_count {0U};
    bool preserve_open_field {true};
    bool repeated_uniform_cards {false};
    bool full_screen_glass_sheet {false};
    bool icon_mask_uniformity_required {false};
};

inline constexpr std::uint8_t max_signature_dominant_surfaces = 3U;
inline constexpr std::uint8_t max_signature_accent_surfaces = 4U;

[[nodiscard]] constexpr bool contour_pair_is_cookie(
    ContourFamily primary,
    ContourFamily accent) noexcept {
    if (primary == accent) return false;
    if (primary == ContourFamily::halo) return false; // Halo is reserved for trust/focus emphasis.
    return accent == ContourFamily::sweep ||
           accent == ContourFamily::pebble ||
           accent == ContourFamily::frame;
}

[[nodiscard]] constexpr bool signature_profile_valid(
    const SignatureProfile& profile) noexcept {
    if (profile.dominant_surface_count == 0U ||
        profile.dominant_surface_count > max_signature_dominant_surfaces) {
        return false;
    }
    if (profile.accent_surface_count > max_signature_accent_surfaces) return false;
    if (!contour_pair_is_cookie(profile.primary_contour, profile.accent_contour)) return false;

    // Cookie must not devolve into a wall of interchangeable rounded cards,
    // a full-screen glass sheet, or a launcher whose identity comes from one
    // mandatory icon mask. Those patterns remain useful research references,
    // not Cookie's signature.
    if (profile.repeated_uniform_cards) return false;
    if (profile.full_screen_glass_sheet) return false;
    if (profile.icon_mask_uniformity_required) return false;

    // Quiet Depth requires visible breathing room. Dense focused surfaces can
    // temporarily fill space, but the default system composition preserves an
    // open field around the dominant object hierarchy.
    if (profile.rhythm == SurfaceRhythm::quiet && !profile.preserve_open_field) {
        return false;
    }
    return true;
}

[[nodiscard]] constexpr SignatureProfile cookie_home_signature() noexcept {
    return SignatureProfile{
        .bias = SpatialBias::lower_weighted,
        .rhythm = SurfaceRhythm::quiet,
        .primary_contour = ContourFamily::anchor,
        .accent_contour = ContourFamily::sweep,
        .motion = MotionCharacter::handoff,
        .dominant_surface_count = 1U,
        .accent_surface_count = 2U,
        .preserve_open_field = true,
        .repeated_uniform_cards = false,
        .full_screen_glass_sheet = false,
        .icon_mask_uniformity_required = false,
    };
}

[[nodiscard]] constexpr SignatureProfile cookie_secure_signature() noexcept {
    return SignatureProfile{
        .bias = SpatialBias::centered,
        .rhythm = SurfaceRhythm::focused,
        .primary_contour = ContourFamily::frame,
        .accent_contour = ContourFamily::halo,
        .motion = MotionCharacter::secure,
        .dominant_surface_count = 1U,
        .accent_surface_count = 1U,
        .preserve_open_field = true,
        .repeated_uniform_cards = false,
        .full_screen_glass_sheet = false,
        .icon_mask_uniformity_required = false,
    };
}

} // namespace os::ui
