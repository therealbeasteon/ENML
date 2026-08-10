#pragma once

#include <os/core/result.hpp>
#include <os/display/types.hpp>
#include <os/package/package.hpp>
#include <os/shell/task_model.hpp>

namespace os::shell {

// Immutable shell->compositor activation claim derived only after the trusted
// shell model has selected an application. It binds one shell revision to the
// signed application identity, exact live process identity, and the
// generation-scoped compositor root surface that were reconciled together.
struct ShellActivationIntent final {
    ShellRevision shell_revision {};
    os::core::ApplicationInstanceId instance {};
    os::package::ApplicationIdentity application {};
    os::core::PeerIdentity owner {};
    os::display::SurfaceId root_surface {};

    [[nodiscard]] bool valid() const noexcept {
        return shell_revision.value() != 0U && instance.value() != 0U &&
            application.valid() && os::core::valid_peer_identity(owner) &&
            os::display::valid_display_object_value(root_surface.value());
    }
};

using ExactActivationCommitFn = os::core::Result<void> (*)(
    void* context,
    os::core::PeerIdentity expected_owner,
    os::display::SurfaceId root_surface) noexcept;

struct ExactActivationBackend final {
    void* context {nullptr};
    ExactActivationCommitFn activate {nullptr};
};

// Capture the current post-navigation activation intent. Home/overview cannot
// manufacture one; the model must already name one exact foreground instance.
[[nodiscard]] os::core::Result<ShellActivationIntent> make_activation_intent(
    const ShellSnapshot& snapshot) noexcept;

// Revalidate the intent immediately before the privileged compositor commit.
// Any shell revision/task/owner/root change turns the old intent stale and the
// backend is not entered. This is the shell equivalent of M3's stale input-hit
// defense: a previously valid object is not a timeless bearer capability.
[[nodiscard]] os::core::Result<void> commit_activation_intent(
    const ShellSnapshot& current,
    const ShellActivationIntent& intent,
    ExactActivationBackend backend) noexcept;

} // namespace os::shell
