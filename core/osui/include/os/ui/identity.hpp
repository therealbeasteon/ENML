#pragma once

#include <cstdint>

namespace os::ui {

// Cookie UI identity roles are semantic policy. Applications may express the
// role they need, but the platform renderer owns the final visual treatment.
enum class PlaneRole : std::uint8_t {
    background = 0U,
    content = 1U,
    control = 2U,
    transient = 3U,
    secure = 4U,
};

// ContourFamily describes Cookie's authored shape language without exposing
// concrete radii or vendor-derived component geometry as application ABI.
enum class ContourFamily : std::uint8_t {
    anchor = 0U,
    sweep = 1U,
    pebble = 2U,
    halo = 3U,
    frame = 4U,
};

// Rendering degrades from ambient toward essential under resource pressure.
// Essential UI remains legible, interactive, attributable, and accessible.
enum class QualityTier : std::uint8_t {
    essential = 0U,
    continuity = 1U,
    material = 2U,
    depth = 3U,
    ambient = 4U,
};

// InputMode changes presentation/focus affordances, not application business
// logic or action ordering.
enum class InputMode : std::uint8_t {
    touch = 0U,
    pointer = 1U,
    keyboard = 2U,
    stylus = 3U,
    switch_access = 4U,
    assistive = 5U,
};

// AttributionRole is renderer-owned for trusted surfaces. It lets the scene
// graph carry provenance without allowing apps to paint a counterfeit system
// security indicator.
enum class AttributionRole : std::uint8_t {
    application = 0U,
    system = 1U,
    privacy = 2U,
    authentication = 3U,
    recovery = 4U,
};

// Semantic surface identity. It deliberately contains no color, radius, blur,
// shadow, font, or vendor-derived paint values.
struct SurfaceIdentity final {
    PlaneRole plane {PlaneRole::content};
    ContourFamily contour {ContourFamily::anchor};
    QualityTier minimum_quality {QualityTier::essential};
    AttributionRole attribution {AttributionRole::application};
    bool interruptible_motion {true};
    bool capture_protected {false};
};

// System-owned secure-plane content must never be paintable by an ordinary
// application surface. Enforcement belongs at the scene/compositor boundary.
[[nodiscard]] constexpr bool platform_owned_plane(PlaneRole role) noexcept {
    return role == PlaneRole::secure;
}

[[nodiscard]] constexpr bool trusted_attribution(AttributionRole role) noexcept {
    return role != AttributionRole::application;
}

[[nodiscard]] constexpr bool optional_quality(QualityTier tier) noexcept {
    return tier == QualityTier::material || tier == QualityTier::depth ||
           tier == QualityTier::ambient;
}

[[nodiscard]] constexpr bool direct_manipulation_input(InputMode mode) noexcept {
    return mode == InputMode::touch || mode == InputMode::pointer ||
           mode == InputMode::stylus;
}

// Quality may only degrade toward essential. This makes pressure handling
// deterministic and prevents decorative effects from outranking interaction.
[[nodiscard]] constexpr QualityTier clamp_quality(
    QualityTier requested,
    QualityTier available) noexcept {
    const auto request = static_cast<std::uint8_t>(requested);
    const auto budget = static_cast<std::uint8_t>(available);
    return static_cast<QualityTier>(request < budget ? request : budget);
}

[[nodiscard]] constexpr SurfaceIdentity content_identity() noexcept {
    return SurfaceIdentity{
        .plane = PlaneRole::content,
        .contour = ContourFamily::anchor,
        .minimum_quality = QualityTier::essential,
        .attribution = AttributionRole::application,
        .interruptible_motion = true,
        .capture_protected = false,
    };
}

[[nodiscard]] constexpr SurfaceIdentity transient_identity() noexcept {
    return SurfaceIdentity{
        .plane = PlaneRole::transient,
        .contour = ContourFamily::sweep,
        .minimum_quality = QualityTier::continuity,
        .attribution = AttributionRole::system,
        .interruptible_motion = true,
        .capture_protected = false,
    };
}

[[nodiscard]] constexpr SurfaceIdentity secure_identity(
    AttributionRole role = AttributionRole::authentication) noexcept {
    return SurfaceIdentity{
        .plane = PlaneRole::secure,
        .contour = ContourFamily::halo,
        .minimum_quality = QualityTier::essential,
        .attribution = role,
        .interruptible_motion = false,
        .capture_protected = true,
    };
}

[[nodiscard]] constexpr bool valid_surface_identity(
    const SurfaceIdentity& identity) noexcept {
    if (identity.plane == PlaneRole::secure) {
        return trusted_attribution(identity.attribution) &&
               identity.minimum_quality == QualityTier::essential &&
               identity.capture_protected;
    }
    if (identity.attribution == AttributionRole::authentication ||
        identity.attribution == AttributionRole::recovery) {
        return false;
    }
    return true;
}

} // namespace os::ui
