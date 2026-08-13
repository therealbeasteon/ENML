#pragma once

#include <os/core/result.hpp>
#include <os/display/types.hpp>

namespace os::shell {

// Exact-process lease for one compositor-authorized system-chrome root. The
// shell does not discover/adopt a surface merely because it looks like chrome
// or carries the same principal label; it accepts only the exact descriptor
// returned for its live PeerIdentity.
struct ShellChromeLease final {
    os::core::PeerIdentity shell {};
    os::display::SurfaceId surface {};
    os::display::Rect bounds {};

    [[nodiscard]] bool valid() const noexcept {
        return os::core::valid_peer_identity(shell) &&
            os::display::valid_display_object_value(surface.value()) &&
            bounds.nonempty();
    }
};

// Convert a compositor-created descriptor into shell-owned chrome authority.
// The descriptor must already be `system_chrome`, parentless, and owned by the
// exact live shell identity. Ordinary app roots/popups/secure-system surfaces
// cannot be rebound into a chrome lease by changing UI metadata.
[[nodiscard]] os::core::Result<ShellChromeLease> accept_system_chrome(
    os::core::PeerIdentity exact_shell,
    const os::display::SurfaceDescriptor& descriptor) noexcept;

// Revalidate an existing lease against authoritative compositor scene state.
// Trust classification, exact owner, role, parent and bounds must still match.
[[nodiscard]] os::core::Result<void> validate_system_chrome(
    const ShellChromeLease& lease,
    const os::display::SceneSnapshot& scene) noexcept;

// Compositor restart recovery is explicit, never automatic scene adoption. A
// replacement must belong to the same exact shell process and a strictly newer
// compositor generation. Shell process replacement must create its own fresh
// lease rather than inheriting a predecessor's authority.
[[nodiscard]] os::core::Result<ShellChromeLease> replace_system_chrome_after_restart(
    const ShellChromeLease& prior,
    const os::display::SurfaceDescriptor& replacement) noexcept;

} // namespace os::shell
