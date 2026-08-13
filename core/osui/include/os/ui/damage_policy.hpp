#pragma once

#include <cstddef>
#include <cstdint>

#include <os/ui/system_composition.hpp>

namespace os::ui {

enum class DamageClass : std::uint8_t {
    none = 0U,
    local = 1U,
    regional = 2U,
    scene = 3U,
};

struct DamageDecision final {
    DamageClass damage {DamageClass::none};
    std::uint8_t dirty_regions {0U};
    bool redraw_wallpaper {false};
    bool redraw_backdrop {false};
    bool preserve_unchanged_surfaces {true};
};

[[nodiscard]] constexpr DamageDecision resolve_damage(
    const SystemSceneComposition& composition,
    std::size_t changed_region_count,
    bool wallpaper_changed,
    bool direct_manipulation) noexcept {
    if (changed_region_count == 0U && !wallpaper_changed) return {};

    DamageDecision decision {};
    decision.dirty_regions = static_cast<std::uint8_t>(
        changed_region_count > composition.count ? composition.count : changed_region_count);
    decision.redraw_wallpaper = wallpaper_changed;
    decision.redraw_backdrop = wallpaper_changed &&
        composition.plan.render.capabilities.live_backdrop;

    if (wallpaper_changed || changed_region_count >= composition.count) {
        decision.damage = DamageClass::scene;
        decision.preserve_unchanged_surfaces = false;
    } else if (changed_region_count > 1U) {
        decision.damage = DamageClass::regional;
    } else {
        decision.damage = DamageClass::local;
    }

    // During direct manipulation, do not expand a small dirty set merely to
    // chase optical effects. Cookie protects touch/frame continuity first.
    if (direct_manipulation && !wallpaper_changed && changed_region_count == 1U) {
        decision.damage = DamageClass::local;
        decision.redraw_backdrop = false;
        decision.preserve_unchanged_surfaces = true;
    }
    return decision;
}

[[nodiscard]] constexpr bool damage_decision_valid(
    const DamageDecision& decision,
    const SystemSceneComposition& composition) noexcept {
    if (decision.dirty_regions > composition.count) return false;
    if (decision.damage == DamageClass::none &&
        (decision.dirty_regions != 0U || decision.redraw_wallpaper || decision.redraw_backdrop)) {
        return false;
    }
    if (decision.redraw_backdrop && !decision.redraw_wallpaper) return false;
    if (decision.damage == DamageClass::scene && decision.preserve_unchanged_surfaces) return false;
    return true;
}

} // namespace os::ui
