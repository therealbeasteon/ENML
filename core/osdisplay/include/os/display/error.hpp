#pragma once

#include <cstdint>

#include <os/core/error.hpp>

namespace os::display {

namespace errors {
inline constexpr std::uint32_t invalid_configuration = 1U;
inline constexpr std::uint32_t invalid_identity = 2U;
inline constexpr std::uint32_t invalid_role = 3U;
inline constexpr std::uint32_t invalid_geometry = 4U;
inline constexpr std::uint32_t surface_limit = 5U;
inline constexpr std::uint32_t principal_surface_limit = 6U;
inline constexpr std::uint32_t unknown_surface = 7U;
inline constexpr std::uint32_t owner_mismatch = 8U;
inline constexpr std::uint32_t invalid_parent = 9U;
inline constexpr std::uint32_t frame_replay = 10U;
inline constexpr std::uint32_t invalid_buffer_slot = 11U;
inline constexpr std::uint32_t invalid_damage = 12U;
inline constexpr std::uint32_t surface_id_exhausted = 13U;
inline constexpr std::uint32_t application_surface_exists = 14U;
inline constexpr std::uint32_t activation_denied = 15U;
inline constexpr std::uint32_t invalid_pixel_format = 16U;
inline constexpr std::uint32_t invalid_buffer = 17U;
inline constexpr std::uint32_t buffer_limit = 18U;
inline constexpr std::uint32_t principal_buffer_limit = 19U;
inline constexpr std::uint32_t buffer_bytes_limit = 20U;
inline constexpr std::uint32_t buffer_owner_mismatch = 21U;
inline constexpr std::uint32_t buffer_size_mismatch = 22U;
inline constexpr std::uint32_t buffer_id_exhausted = 23U;
inline constexpr std::uint32_t buffer_create_failed = 24U;
inline constexpr std::uint32_t stale_input_hit = 25U;
}

[[nodiscard]] constexpr os::core::Error display_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::display, code);
}

} // namespace os::display
