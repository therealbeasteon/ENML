#pragma once

#include <cstdint>

#include <os/core/error.hpp>

namespace os::shell {

namespace errors {
inline constexpr std::uint32_t invalid_task = 1U;
inline constexpr std::uint32_t task_capacity = 2U;
inline constexpr std::uint32_t task_conflict = 3U;
inline constexpr std::uint32_t unknown_task = 4U;
inline constexpr std::uint32_t revision_exhausted = 5U;
inline constexpr std::uint32_t activation_serial_exhausted = 6U;
inline constexpr std::uint32_t invalid_lifecycle_snapshot = 7U;
inline constexpr std::uint32_t stale_lifecycle_snapshot = 8U;
inline constexpr std::uint32_t invalid_scene_snapshot = 9U;
inline constexpr std::uint32_t stale_scene_snapshot = 10U;
} // namespace errors

[[nodiscard]] constexpr os::core::Error shell_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::shell, code);
}

} // namespace os::shell
