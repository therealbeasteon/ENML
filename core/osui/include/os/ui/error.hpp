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
}

[[nodiscard]] constexpr os::core::Error ui_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ui, code);
}

} // namespace os::ui
