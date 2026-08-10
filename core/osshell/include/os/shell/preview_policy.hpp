#pragma once

#include <os/core/result.hpp>
#include <os/display/types.hpp>
#include <os/shell/task_model.hpp>

namespace os::shell {

// Ephemeral permission to sample one already-presented application frame for a
// transient shell preview. This contains no pixels, mapped memory, file handle,
// cache key, path, timestamp, or persistence request. The actual capture layer
// must revalidate it immediately before reading a compositor-owned frame.
struct TaskPreviewGrant final {
    ShellRevision shell_revision {};
    os::core::ApplicationInstanceId instance {};
    os::core::PeerIdentity owner {};
    os::display::SurfaceId root_surface {};
    os::display::BufferId buffer {};
    std::uint64_t frame_sequence {0U};

    [[nodiscard]] bool valid() const noexcept {
        return shell_revision.value() != 0U && instance.value() != 0U &&
            os::core::valid_peer_identity(owner) &&
            os::display::valid_display_object_value(root_surface.value()) &&
            os::display::valid_display_object_value(buffer.value()) &&
            frame_sequence != 0U;
    }
};

// Conservative default policy for M4: only a currently visible, exact
// application-root frame with compositor capture permission and no trusted
// presentation can receive a transient preview grant. Hidden tasks fall back to
// semantic Home/Overview cards rather than retaining screenshots solely for
// recents. Secure-system/system-chrome pixels and popups are never candidates.
[[nodiscard]] os::core::Result<TaskPreviewGrant> authorize_task_preview(
    const ShellSnapshot& shell,
    const os::display::SceneSnapshot& scene,
    os::core::ApplicationInstanceId instance) noexcept;

// Revalidate the full shell/scene/frame binding immediately before sampling.
// Any shell revision, exact owner, surface, buffer, frame, visibility, role,
// trust, or capture-policy change invalidates the old grant. A grant is not a
// timeless bearer capability and should never be serialized or persisted.
[[nodiscard]] os::core::Result<void> validate_task_preview_grant(
    const ShellSnapshot& shell,
    const os::display::SceneSnapshot& scene,
    const TaskPreviewGrant& grant) noexcept;

} // namespace os::shell
