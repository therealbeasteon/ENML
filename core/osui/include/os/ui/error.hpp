#pragma once

#include <cstdint>

#include <os/core/error.hpp>

namespace os::ui {

namespace errors {
inline constexpr std::uint32_t invalid_tree = 1U;
inline constexpr std::uint32_t invalid_node = 2U;
inline constexpr std::uint32_t node_limit = 3U;
inline constexpr std::uint32_t child_limit = 4U;
inline constexpr std::uint32_t depth_limit = 5U;
inline constexpr std::uint32_t invalid_parent = 6U;
inline constexpr std::uint32_t invalid_role = 7U;
inline constexpr std::uint32_t invalid_bounds = 8U;
inline constexpr std::uint32_t invalid_text = 9U;
inline constexpr std::uint32_t text_too_long = 10U;
inline constexpr std::uint32_t invalid_state = 11U;
inline constexpr std::uint32_t invalid_action = 12U;
inline constexpr std::uint32_t root_immutable = 13U;
inline constexpr std::uint32_t no_focus = 14U;
inline constexpr std::uint32_t invalid_viewport = 15U;
inline constexpr std::uint32_t id_exhausted = 16U;
inline constexpr std::uint32_t invalid_style = 17U;
inline constexpr std::uint32_t invalid_collection = 18U;
inline constexpr std::uint32_t collection_window_limit = 19U;
inline constexpr std::uint32_t invalid_text_scale = 20U;
inline constexpr std::uint32_t invalid_render_snapshot = 21U;
inline constexpr std::uint32_t invalid_render_options = 22U;
inline constexpr std::uint32_t invalid_text_shape = 23U;
inline constexpr std::uint32_t text_shape_limit = 24U;
inline constexpr std::uint32_t text_shaper_unavailable = 25U;
inline constexpr std::uint32_t text_shaper_failed = 26U;
inline constexpr std::uint32_t invalid_collection_source = 27U;
inline constexpr std::uint32_t stale_collection_snapshot = 28U;
inline constexpr std::uint32_t invalid_raster_target = 29U;
inline constexpr std::uint32_t invalid_raster_theme = 30U;
inline constexpr std::uint32_t invalid_raster_command = 31U;
inline constexpr std::uint32_t font_provider_unavailable = 32U;
inline constexpr std::uint32_t font_provider_failed = 33U;
inline constexpr std::uint32_t invalid_font_face = 34U;
inline constexpr std::uint32_t invalid_motion_timeline = 35U;
inline constexpr std::uint32_t glyph_provider_unavailable = 36U;
inline constexpr std::uint32_t glyph_provider_failed = 37U;
inline constexpr std::uint32_t invalid_glyph_mask = 38U;
inline constexpr std::uint32_t invalid_paragraph_layout = 39U;
inline constexpr std::uint32_t paragraph_layout_limit = 40U;
inline constexpr std::uint32_t paragraph_backend_unavailable = 41U;
inline constexpr std::uint32_t paragraph_backend_failed = 42U;
inline constexpr std::uint32_t invalid_collection_change = 43U;
inline constexpr std::uint32_t collection_change_source_failed = 44U;
inline constexpr std::uint32_t font_line_metrics_unavailable = 45U;
inline constexpr std::uint32_t font_line_metrics_failed = 46U;
inline constexpr std::uint32_t invalid_font_line_metrics = 47U;
inline constexpr std::uint32_t invalid_input_snapshot = 48U;
inline constexpr std::uint32_t invalid_input_point = 49U;
inline constexpr std::uint32_t invalid_input_action = 50U;
inline constexpr std::uint32_t input_no_target = 51U;
inline constexpr std::uint32_t invalid_input_transform = 52U;
inline constexpr std::uint32_t invalid_accessibility_snapshot = 53U;
inline constexpr std::uint32_t stale_accessibility_snapshot = 54U;
inline constexpr std::uint32_t invalid_accessibility_action = 55U;
inline constexpr std::uint32_t invalid_collection_content = 56U;
inline constexpr std::uint32_t accessibility_authority_denied = 57U;
inline constexpr std::uint32_t invalid_render_damage = 58U;
inline constexpr std::uint32_t accessibility_session_mismatch = 59U;
inline constexpr std::uint32_t invalid_decision_layout = 60U;
inline constexpr std::uint32_t missing_negative_choice = 61U;
inline constexpr std::uint32_t undersized_decision_target = 62U;
inline constexpr std::uint32_t overlapping_decision_targets = 63U;
inline constexpr std::uint32_t insufficient_target_separation = 64U;
inline constexpr std::uint32_t disproportionate_decision_choice = 65U;
}

[[nodiscard]] constexpr os::core::Error ui_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ui, code);
}

} // namespace os::ui
