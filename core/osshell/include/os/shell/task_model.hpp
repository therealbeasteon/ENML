#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/app/lifecycle.hpp>
#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/core/strong_id.hpp>
#include <os/display/types.hpp>
#include <os/package/package.hpp>

namespace os::shell {

// The shell task ceiling is exactly the lifecycle capacity published by App
// Manager. Product navigation must not grow a second, larger hidden registry.
inline constexpr std::size_t max_shell_tasks = os::app::max_application_lifecycle_instances;

struct ShellRevisionTag;
using ShellRevision = os::core::StrongId<ShellRevisionTag, std::uint64_t>;

enum class ShellView : std::uint8_t {
    home = 1U,
    application = 2U,
    overview = 3U,
};

// Trusted shell metadata for one fully joined live application root. This uses
// semantic/generation-scoped ENML identities only: no native PID, executable
// path, Linux window handle or vendor task identifier becomes shell ABI.
struct ShellTask final {
    os::core::ApplicationInstanceId instance {};
    os::package::ApplicationIdentity application {};
    os::core::PeerIdentity owner {};
    os::display::SurfaceId root_surface {};
    std::uint64_t activation_serial {0U};

    [[nodiscard]] bool valid() const noexcept {
        return instance.value() != 0U && application.valid() &&
            os::core::valid_peer_identity(owner) &&
            os::display::valid_display_object_value(root_surface.value());
    }
};

struct ShellSnapshot final {
    ShellRevision revision {};
    ShellView view {ShellView::home};
    os::core::ApplicationInstanceId active_instance {};
    std::array<ShellTask, max_shell_tasks> tasks {};
    std::size_t task_count {0U};
};

// Event-driven shell state. There is no task scanner, process poller or timer:
// trusted lifecycle/display integration publishes/removes exact live tasks and
// user/system navigation drives view transitions. The later shell process will
// authenticate those integration crossings rather than exposing this model to
// ordinary applications.
class ShellTaskModel final {
public:
    ShellTaskModel() noexcept = default;

    ShellTaskModel(const ShellTaskModel&) = delete;
    ShellTaskModel& operator=(const ShellTaskModel&) = delete;

    // Publishes a live application root directly. This remains useful for
    // focused internal tests/adapters; production shell integration should
    // prefer reconcile() so one source cannot self-assert both lifecycle and
    // compositor identity.
    [[nodiscard]] os::core::Result<void> publish(ShellTask task) noexcept;

    // Coherently joins App Manager lifecycle state with compositor scene state.
    // Only an exact live lifecycle identity that also owns exactly one
    // application root surface becomes a task. Orphan surfaces and lifecycle
    // entries without a root are omitted. The whole desired task set is
    // committed with one shell revision, preserving activation serials for
    // exact surviving identities.
    [[nodiscard]] os::core::Result<void> reconcile(
        const os::app::ApplicationLifecycleSnapshot& applications,
        const os::display::SceneSnapshot& scene) noexcept;

    // Removes one exact application instance. Removing the active application
    // returns the shell to home rather than selecting another task implicitly.
    [[nodiscard]] os::core::Result<void>
    remove(os::core::ApplicationInstanceId instance) noexcept;

    // Selects a known task as foreground shell intent. This updates MRU serial
    // but does not itself call the compositor; a later authenticated shell
    // authority commits this semantic intent to compositor activation.
    [[nodiscard]] os::core::Result<void>
    activate(os::core::ApplicationInstanceId instance) noexcept;

    [[nodiscard]] os::core::Result<void> show_home() noexcept;
    [[nodiscard]] os::core::Result<void> show_overview() noexcept;

    [[nodiscard]] ShellSnapshot snapshot() const noexcept;

private:
    std::array<ShellTask, max_shell_tasks> tasks_ {};
    std::size_t task_count_ {0U};
    ShellRevision revision_ {1U};
    ShellView view_ {ShellView::home};
    os::core::ApplicationInstanceId active_instance_ {};
    std::uint64_t next_activation_serial_ {1U};
    std::uint64_t last_lifecycle_revision_ {0U};

    [[nodiscard]] ShellTask* find(os::core::ApplicationInstanceId instance) noexcept;
    [[nodiscard]] const ShellTask* find(os::core::ApplicationInstanceId instance) const noexcept;
    [[nodiscard]] os::core::Result<void> advance_revision() noexcept;
};

} // namespace os::shell
