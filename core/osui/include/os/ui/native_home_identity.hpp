#pragma once

#include <cstdint>

namespace os::ui {

// Cookie Home is a spatial launcher, not a page grid. Applications, people,
// documents, actions and Living Tiles inhabit an authored field around stable
// anchors. Reachability changes placement, never the identity grammar.
enum class HomeFlow : std::uint8_t {
    anchor_field = 0U,      // primary Cookie grammar
    contour_stream = 1U,   // contextual objects follow a Sweep path
    focus_orbit = 2U,      // temporary task/search context around an anchor
};

enum class HomeGrouping : std::uint8_t {
    proximity = 0U,
    contour = 1U,
    semantic_cluster = 2U,
};

enum class ShelfPresence : std::uint8_t {
    hidden = 0U,
    edge_hint = 1U,
    contextual = 2U,
};

struct NativeHomeIdentity final {
    HomeFlow flow {HomeFlow::anchor_field};
    HomeGrouping grouping {HomeGrouping::contour};
    ShelfPresence shelf {ShelfPresence::contextual};
    bool fixed_page_grid {false};
    bool permanent_bottom_tray {false};
    bool uniform_icon_mask_required {false};
    bool card_wall_grouping {false};
    bool oversized_blank_header_required {false};
    bool full_screen_glass_required {false};
    bool wallpaper_required_for_identity {false};
    bool semantic_objects {true};
    bool stable_spatial_anchors {true};
    bool edge_reveal_discovery {true};
};

[[nodiscard]] constexpr NativeHomeIdentity cookie_native_home_identity() noexcept {
    return {};
}

[[nodiscard]] constexpr bool native_home_identity_valid(
    const NativeHomeIdentity& identity) noexcept {
    // Explicit anti-convergence rules: these structures make Cookie read as a
    // conventional Android/iOS/One-UI-style launcher rather than Cookie Home.
    if (identity.fixed_page_grid || identity.permanent_bottom_tray ||
        identity.uniform_icon_mask_required || identity.card_wall_grouping ||
        identity.oversized_blank_header_required ||
        identity.full_screen_glass_required ||
        identity.wallpaper_required_for_identity) {
        return false;
    }
    if (!identity.semantic_objects || !identity.stable_spatial_anchors ||
        !identity.edge_reveal_discovery) {
        return false;
    }
    return identity.flow == HomeFlow::anchor_field &&
           identity.grouping != HomeGrouping::proximity;
}

// Personalization may alter density and emphasis, but it may not replace the
// launcher grammar with arbitrary combinations that become impossible to QA.
struct HomePersonalizationBudget final {
    std::uint8_t density_steps {3U};
    std::uint8_t anchor_variants {3U};
    bool icon_scale_adjustable {true};
    bool contour_emphasis_adjustable {true};
    bool arbitrary_layout_engine_replaceable {false};
};

[[nodiscard]] constexpr bool personalization_budget_valid(
    const HomePersonalizationBudget& budget) noexcept {
    return budget.density_steps >= 2U && budget.density_steps <= 5U &&
           budget.anchor_variants >= 2U && budget.anchor_variants <= 5U &&
           !budget.arbitrary_layout_engine_replaceable;
}

} // namespace os::ui
