#pragma once

#include <cstdint>

namespace os::ui {

// Historical mobile UI research (not visual cloning) contributes discipline:
// glanceability, predictable navigation, bounded density, and fast task return.
enum class GlanceDensity : std::uint8_t {
    sparse = 0U,
    balanced = 1U,
    dense = 2U,
};

enum class TaskReturnMode : std::uint8_t {
    spatial_handoff = 0U,
    live_preview = 1U,
};

struct MobileDisciplinePolicy final {
    GlanceDensity home_density {GlanceDensity::balanced};
    GlanceDensity notification_density {GlanceDensity::dense};
    bool primary_navigation_predictable {true};
    bool one_hand_core_actions {true};
    bool informative_lock_state {true};
    bool widgets_glanceable {true};
    bool visual_task_previews {true};
    bool text_softkey_bar {false};
    bool legacy_menu_tree {false};
    bool multi_page_home_required {false};
    bool rectangular_widget_chrome_required {false};
};

[[nodiscard]] constexpr bool mobile_discipline_policy_valid(
    const MobileDisciplinePolicy& policy) noexcept {
    return policy.primary_navigation_predictable &&
           policy.one_hand_core_actions &&
           policy.informative_lock_state &&
           policy.widgets_glanceable &&
           policy.visual_task_previews &&
           !policy.text_softkey_bar &&
           !policy.legacy_menu_tree &&
           !policy.multi_page_home_required &&
           !policy.rectangular_widget_chrome_required;
}

[[nodiscard]] constexpr TaskReturnMode preferred_task_return(bool memory_pressure) noexcept {
    return memory_pressure ? TaskReturnMode::spatial_handoff : TaskReturnMode::live_preview;
}

} // namespace os::ui
