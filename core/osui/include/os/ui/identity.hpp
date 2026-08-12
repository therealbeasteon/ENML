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

// System-owned secure-plane content must never be paintable by an ordinary
// application surface. Enforcement belongs at the scene/compositor boundary.
[[nodiscard]] constexpr bool platform_owned_plane(PlaneRole role) noexcept {
    return role == PlaneRole::secure;
}

[[nodiscard]] constexpr bool optional_quality(QualityTier tier) noexcept {
    return tier == QualityTier::material || tier == QualityTier::depth ||
           tier == QualityTier::ambient;
}

[[nodiscard]] constexpr bool direct_manipulation_input(InputMode mode) noexcept {
    return mode == InputMode::touch || mode == InputMode::pointer ||
           mode == InputMode::stylus;
}

} // namespace os::ui
