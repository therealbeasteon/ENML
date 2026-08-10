#pragma once

#include <cstddef>
#include <cstdint>

#include <os/app/lifecycle.hpp>
#include <os/app/manager.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/rpc.hpp>

namespace os::app {

// Private App Manager control namespace for the trusted phone shell. Knowledge
// of this numeric ServiceId is not authority: every request is authenticated
// from SCM_CREDENTIALS through the trusted identity resolver before any
// lifecycle state is returned.
inline constexpr os::core::ServiceId shell_lifecycle_control_service_id{0x0000F016U};
inline constexpr std::uint32_t shell_lifecycle_operation_snapshot = 1U;

using ShellLifecycleSnapshotFn = os::core::Result<ApplicationLifecycleSnapshot> (*)(
    void* context) noexcept;

// Internal composition seam only. Function pointers never cross IPC.
struct ShellLifecycleBackend final {
    void* context {nullptr};
    ShellLifecycleSnapshotFn snapshot {nullptr};
};

[[nodiscard]] ShellLifecycleBackend shell_lifecycle_backend(
    ApplicationManager& manager) noexcept;

class ShellLifecycleControlServer final {
public:
    ShellLifecycleControlServer(
        ShellLifecycleBackend backend,
        os::ipc::PeerIdentityResolver& identity_resolver) noexcept
        : backend_(backend), identity_resolver_(&identity_resolver) {}

    [[nodiscard]] bool valid() const noexcept {
        return backend_.snapshot != nullptr && identity_resolver_ != nullptr;
    }

    // Handles exactly one bounded request. Unauthorized callers receive one
    // generic access-denied result before operation or payload validation so the
    // interface cannot be used as an application-lifecycle enumeration oracle.
    [[nodiscard]] os::core::Result<void> dispatch_once(
        os::ipc::Channel& channel,
        os::core::MutableByteSpan scratch) noexcept;

private:
    ShellLifecycleBackend backend_ {};
    os::ipc::PeerIdentityResolver* identity_resolver_ {nullptr};
};

class ShellLifecycleControlClient final {
public:
    explicit ShellLifecycleControlClient(os::ipc::Channel& channel) noexcept
        : connection_(channel) {}

    [[nodiscard]] os::core::Result<ApplicationLifecycleSnapshot> snapshot(
        os::core::MutableByteSpan scratch) noexcept;

private:
    os::ipc::ClientConnection connection_;
};

} // namespace os::app
