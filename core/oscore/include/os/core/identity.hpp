#pragma once

#include <os/core/strong_id.hpp>

namespace os::core {

// Public EMNL runtime identity. These values are resolved from trusted
// supervisor/process state; applications never establish them through an IPC
// payload field.
struct PeerIdentity final {
    PrincipalId principal {};
    UserId user {};
    ProcessId process {};

    [[nodiscard]] friend constexpr bool
    operator==(const PeerIdentity&, const PeerIdentity&) = default;
};

[[nodiscard]] constexpr bool valid_principal(PrincipalId principal) noexcept {
    return principal.high != 0U || principal.low != 0U;
}

[[nodiscard]] constexpr bool valid_peer_identity(const PeerIdentity& identity) noexcept {
    return valid_principal(identity.principal) && identity.process.value() != 0U;
}

} // namespace os::core
