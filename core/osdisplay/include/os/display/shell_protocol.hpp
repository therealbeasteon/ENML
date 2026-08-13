#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/strong_id.hpp>

namespace os::display {

// Private trusted-shell -> compositor control namespace. These values describe
// wire shape only; authority is always resolved from the live kernel sender by
// the compositor control server before request payload is interpreted.
inline constexpr os::core::ServiceId shell_compositor_control_service_id{0x0000F031U};
inline constexpr std::uint32_t shell_compositor_operation_activate_exact = 1U;
inline constexpr std::size_t shell_compositor_activate_request_size_v1 = 40U;

} // namespace os::display
