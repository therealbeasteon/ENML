#pragma once

#include <os/core/strong_id.hpp>

namespace os::shell {

// Trusted phone-shell process identity. This ServiceId is an internal protocol
// label used for Supervisor bootstrap; it is not application-discoverable
// authority and does not grant shell privileges by itself.
inline constexpr os::core::ServiceId shell_service_id{0x0000F040U};

} // namespace os::shell
