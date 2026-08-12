#pragma once

#include <cstdint>

#include <os/ui/device_profile.hpp>
#include <os/ui/identity.hpp>
#include <os/ui/signature.hpp>

namespace os::ui {

enum class SystemSceneKind : std::uint8_t {
    lock_screen = 0U,
    home = 1U,
    quick_controls = 2U,
    notifications = 3U,
    settings = 4U,
    app_surface = 5U,
    app_switcher = 6U,
};

struct SystemSceneGrammar final {
    SystemSceneKind kind {SystemSceneKind::home};
    SignatureProfile signature {};
    PlaneRole dominant_plane {PlaneRole::content};
    ContourFamily navigation_contour {ContourFamily::frame};
    MotionCharacter entry_motion {MotionCharacter::reveal};
    bool lower_reach_priority {false};
    bool wallpaper_context_visible {false};
    bool allows_dense_content {false};
    bool trusted_attribution_required {false};
    bool direct_manipulation_preferred {true};
};

[[nodiscard]] constexpr SystemSceneGrammar scene_grammar(
    SystemSceneKind kind,
    const DeviceProfile& profile) noexcept {
    const bool compact = profile.width_class == WidthClass::compact;
    const bool tall = profile.height_class == HeightClass::tall;

    switch (kind) {
    case SystemSceneKind::lock_screen:
        return SystemSceneGrammar{
            .kind = kind,
            .signature = SignatureProfile{
                .bias = SpatialBias::asymmetric_balanced,
                .rhythm = SurfaceRhythm::quiet,
                .primary_contour = ContourFamily::frame,
                .accent_contour = ContourFamily::sweep,
                .motion = MotionCharacter::reveal,
                .dominant_surface_count = 1U,
                .accent_surface_count = 2U,
                .preserve_open_field = true,
            },
            .dominant_plane = PlaneRole::background,
            .navigation_contour = ContourFamily::frame,
            .entry_motion = MotionCharacter::reveal,
            .lower_reach_priority = compact && tall,
            .wallpaper_context_visible = true,
            .allows_dense_content = false,
            .trusted_attribution_required = true,
            .direct_manipulation_preferred = true,
        };
    case SystemSceneKind::home:
        return SystemSceneGrammar{
            .kind = kind,
            .signature = cookie_home_signature(),
            .dominant_plane = PlaneRole::content,
            .navigation_contour = ContourFamily::sweep,
            .entry_motion = MotionCharacter::handoff,
            .lower_reach_priority = compact,
            .wallpaper_context_visible = true,
            .allows_dense_content = false,
            .trusted_attribution_required = false,
            .direct_manipulation_preferred = true,
        };
    case SystemSceneKind::quick_controls:
        return SystemSceneGrammar{
            .kind = kind,
            .signature = SignatureProfile{
                .bias = SpatialBias::lower_weighted,
                .rhythm = SurfaceRhythm::grouped,
                .primary_contour = ContourFamily::anchor,
                .accent_contour = ContourFamily::pebble,
                .motion = MotionCharacter::settle,
                .dominant_surface_count = 2U,
                .accent_surface_count = 3U,
                .preserve_open_field = false,
            },
            .dominant_plane = PlaneRole::control,
            .navigation_contour = ContourFamily::pebble,
            .entry_motion = MotionCharacter::settle,
            .lower_reach_priority = true,
            .wallpaper_context_visible = false,
            .allows_dense_content = true,
            .trusted_attribution_required = true,
            .direct_manipulation_preferred = true,
        };
    case SystemSceneKind::notifications:
        return SystemSceneGrammar{
            .kind = kind,
            .signature = SignatureProfile{
                .bias = SpatialBias::edge_anchored,
                .rhythm = SurfaceRhythm::flowing,
                .primary_contour = ContourFamily::frame,
                .accent_contour = ContourFamily::sweep,
                .motion = MotionCharacter::reveal,
                .dominant_surface_count = 2U,
                .accent_surface_count = 2U,
                .preserve_open_field = true,
            },
            .dominant_plane = PlaneRole::transient,
            .navigation_contour = ContourFamily::frame,
            .entry_motion = MotionCharacter::reveal,
            .lower_reach_priority = compact,
            .wallpaper_context_visible = false,
            .allows_dense_content = true,
            .trusted_attribution_required = true,
            .direct_manipulation_preferred = true,
        };
    case SystemSceneKind::settings:
        return SystemSceneGrammar{
            .kind = kind,
            .signature = SignatureProfile{
                .bias = SpatialBias::asymmetric_balanced,
                .rhythm = SurfaceRhythm::grouped,
                .primary_contour = ContourFamily::frame,
                .accent_contour = ContourFamily::pebble,
                .motion = MotionCharacter::handoff,
                .dominant_surface_count = 2U,
                .accent_surface_count = 2U,
                .preserve_open_field = true,
            },
            .dominant_plane = PlaneRole::content,
            .navigation_contour = ContourFamily::frame,
            .entry_motion = MotionCharacter::handoff,
            .lower_reach_priority = compact,
            .wallpaper_context_visible = false,
            .allows_dense_content = true,
            .trusted_attribution_required = true,
            .direct_manipulation_preferred = true,
        };
    case SystemSceneKind::app_surface:
        return SystemSceneGrammar{
            .kind = kind,
            .signature = SignatureProfile{
                .bias = SpatialBias::asymmetric_balanced,
                .rhythm = SurfaceRhythm::quiet,
                .primary_contour = ContourFamily::anchor,
                .accent_contour = ContourFamily::frame,
                .motion = MotionCharacter::handoff,
                .dominant_surface_count = 1U,
                .accent_surface_count = 1U,
                .preserve_open_field = true,
            },
            .dominant_plane = PlaneRole::content,
            .navigation_contour = ContourFamily::frame,
            .entry_motion = MotionCharacter::handoff,
            .lower_reach_priority = compact,
            .wallpaper_context_visible = false,
            .allows_dense_content = true,
            .trusted_attribution_required = false,
            .direct_manipulation_preferred = true,
        };
    case SystemSceneKind::app_switcher:
        return SystemSceneGrammar{
            .kind = kind,
            .signature = SignatureProfile{
                .bias = SpatialBias::asymmetric_balanced,
                .rhythm = SurfaceRhythm::flowing,
                .primary_contour = ContourFamily::frame,
                .accent_contour = ContourFamily::sweep,
                .motion = MotionCharacter::handoff,
                .dominant_surface_count = 2U,
                .accent_surface_count = 2U,
                .preserve_open_field = true,
            },
            .dominant_plane = PlaneRole::transient,
            .navigation_contour = ContourFamily::sweep,
            .entry_motion = MotionCharacter::handoff,
            .lower_reach_priority = compact,
            .wallpaper_context_visible = true,
            .allows_dense_content = false,
            .trusted_attribution_required = true,
            .direct_manipulation_preferred = true,
        };
    }
    return {};
}

[[nodiscard]] constexpr bool system_scene_grammar_valid(
    const SystemSceneGrammar& grammar) noexcept {
    if (!signature_profile_valid(grammar.signature)) return false;
    if (grammar.kind == SystemSceneKind::lock_screen &&
        !grammar.trusted_attribution_required) return false;
    if (grammar.kind == SystemSceneKind::quick_controls &&
        grammar.dominant_plane != PlaneRole::control) return false;
    if (grammar.kind == SystemSceneKind::app_switcher &&
        grammar.entry_motion != MotionCharacter::handoff) return false;
    if (grammar.kind == SystemSceneKind::app_surface &&
        grammar.trusted_attribution_required) return false;
    return true;
}

} // namespace os::ui
