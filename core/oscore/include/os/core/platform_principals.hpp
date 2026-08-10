#pragma once

#include <os/core/strong_id.hpp>

namespace os::core {

// Canonical ENML trusted-system principals. These numeric identifiers are
// labels resolved by Supervisor/runtime identity; knowledge of a value is never
// authority. Privileged RPC must still derive the live PeerIdentity from kernel
// credentials and compare its principal against the appropriate constant.
//
// Keeping these labels in one low-level header avoids configuration drift where
// two trusted components accidentally disagree about which principal owns a
// security boundary.
inline constexpr PrincipalId shell_service_principal{
    0x454E4D4C5348454CULL,
    0x4C00000000000001ULL,
};

inline constexpr PrincipalId secure_ui_service_principal{
    0x454E4D4C53454355ULL,
    0x5245554900000001ULL,
};

static_assert(shell_service_principal != secure_ui_service_principal);

} // namespace os::core
