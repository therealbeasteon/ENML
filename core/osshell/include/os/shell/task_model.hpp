#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/core/strong_id.hpp>
#include <os/display/types.hpp>
#include <os/package/package.hpp>

namespace os::shell {

// M4.0 begins with the same hard live-application ceiling already owned by App
// Manager. The shell must not create a second, larger hidden task registry that
// can grow independently of the process/lifecycle authority beneath it.
inline constexpr std::size_t max_shell_tasks = 16U;

struct ShellRevisionTag;
using ShellRevision = os::core::StrongId<ShellRevisionTag, std::uint64_t>;

enum class ShellView : std::uint8_t {
    home = 1U,
    application = 2U,
    overview = 3U,
};

// Trusted shell metadata for one live application root. This deliberately uses
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

    // Publishes a live application root. Re-publishing the same exact
    // instance/application/owner updates only its generation-scoped root
    // surface, allowing compositor restart recovery without inventing a new
    // application task. Rebinding an instance to another identity fails closed.
    [[nodiscard]] os::core::Result<void> publish(ShellTask task) noexcept;

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

    [[nodiscard]] ShellTask* find(os::core::ApplicationInstanceId instance) noexcept;
    [[nodiscard]] const ShellTask* find(os::core::ApplicationInstanceId instance) const noexcept;
    [[nodiscard]] os::core::Result<void> advance_revision() noexcept;
};

} // namespace os::shell
