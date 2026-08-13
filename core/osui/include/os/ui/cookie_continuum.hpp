#pragma once

#include <cstdint>

#include <os/ui/device_profile.hpp>

namespace os::ui {

enum class ContinuumPrimitive : std::uint8_t {
    field = 0U,
    thread = 1U,
    node = 2U,
    seal = 3U,
    veil = 4U,
};

enum class ContinuumBand : std::uint8_t { now = 0U, pinned = 1U, index = 2U };
enum class IndexView : std::uint8_t { alphabetic = 0U, intent = 1U, search = 2U };
enum class ThreadSide : std::uint8_t { left = 0U, right = 1U, split = 2U };
enum class ThreadRelation : std::uint8_t {
    none = 0U,
    continuation = 1U,
    contextual = 2U,
    user_group = 3U,
    trusted = 4U,
};

struct ContinuumNode final {
    std::uint64_t object_id {0U};
    ContinuumBand band {ContinuumBand::pinned};
    std::uint16_t anchor_x_q10 {512U};
    std::uint16_t anchor_y_q10 {512U};
    std::uint16_t visual_weight_q10 {512U};
    bool user_stable {false};
    bool trusted_system {false};
};

struct ContinuumThread final {
    std::uint64_t from_object_id {0U};
    std::uint64_t to_object_id {0U};
    ThreadRelation relation {ThreadRelation::none};
    bool permanently_visible {false};
};

[[nodiscard]] constexpr bool continuum_node_valid(const ContinuumNode& node) noexcept {
    if (node.object_id == 0U || node.anchor_x_q10 > 1024U || node.anchor_y_q10 > 1024U ||
        node.visual_weight_q10 == 0U || node.visual_weight_q10 > 1024U) return false;
    if (node.band == ContinuumBand::pinned && !node.user_stable && !node.trusted_system) return false;
    return true;
}

[[nodiscard]] constexpr bool continuum_thread_valid(const ContinuumThread& thread) noexcept {
    if (thread.from_object_id == 0U || thread.to_object_id == 0U ||
        thread.from_object_id == thread.to_object_id || thread.relation == ThreadRelation::none) return false;
    // Thread is primarily a relational/motion primitive, not permanent decorative wiring.
    if (thread.permanently_visible && thread.relation != ThreadRelation::trusted) return false;
    return true;
}

struct CookieContinuumPolicy final {
    bool single_continuous_home {true};
    bool permanent_icon_dock {false};
    bool fixed_page_grid {false};
    bool card_wall_primary {false};
    bool glass_required {false};
    bool uniform_icon_mask_required {false};
    bool bottom_sheet_primary_navigation {false};
    bool stable_spatial_order_required {true};
    bool semantic_index_required {true};
    bool seal_reserved_for_trusted_ui {true};
};

struct CookieIndexPolicy final {
    IndexView default_view {IndexView::intent};
    ThreadSide side {ThreadSide::right};
    bool alphabetic_fallback {true};
    bool local_search {true};
    bool contacts {true};
    bool conversations {true};
    bool documents {true};
    bool settings {true};
    bool actions {true};
    bool prediction_may_reorder_main_index {false};
};

[[nodiscard]] constexpr ThreadSide resolve_thread_side(const DeviceProfile& profile, bool prefer_left_hand) noexcept {
    if (profile.cutout == CutoutKind::hinge || profile.posture == DevicePosture::unfolded) return ThreadSide::split;
    return prefer_left_hand ? ThreadSide::left : ThreadSide::right;
}

[[nodiscard]] constexpr CookieIndexPolicy resolve_cookie_index(const DeviceProfile& profile, bool prefer_left_hand) noexcept {
    CookieIndexPolicy policy {};
    policy.side = resolve_thread_side(profile, prefer_left_hand);
    return policy;
}

[[nodiscard]] constexpr bool cookie_continuum_policy_valid(const CookieContinuumPolicy& policy) noexcept {
    return policy.single_continuous_home && !policy.permanent_icon_dock && !policy.fixed_page_grid &&
           !policy.card_wall_primary && !policy.glass_required && !policy.uniform_icon_mask_required &&
           !policy.bottom_sheet_primary_navigation && policy.stable_spatial_order_required &&
           policy.semantic_index_required && policy.seal_reserved_for_trusted_ui;
}

[[nodiscard]] constexpr bool cookie_index_policy_valid(const CookieIndexPolicy& policy) noexcept {
    if (!policy.alphabetic_fallback || !policy.local_search) return false;
    if (policy.prediction_may_reorder_main_index) return false;
    return true;
}

} // namespace os::ui
