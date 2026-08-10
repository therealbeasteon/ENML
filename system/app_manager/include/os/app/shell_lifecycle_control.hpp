#pragma once

#include <cstddef>

#include <os/app/manager.hpp>
#include <os/app/shell_lifecycle_client.hpp>
#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/rpc.hpp>

namespace os::app {

using ShellLifecycleSnapshotFn = os::core::Result<ApplicationLifecycleSnapshot> (*)(
    void* context) noexcept;
using ShellTakeCompositorCapabilityFn = os::core::Result<os::core::NativeHandle> (*)(
    void* context) noexcept;

// Internal composition seam only. Function pointers and backing contexts never
// cross IPC. `context` remains the lifecycle context for compatibility with the
// existing server fixture shape; compositor handoff has its own optional
// context so boot composition does not teach ApplicationManager about display.
struct ShellLifecycleBackend final {
    void* context {nullptr};
    ShellLifecycleSnapshotFn snapshot {nullptr};
    void* compositor_context {nullptr};
    ShellTakeCompositorCapabilityFn take_compositor_capability {nullptr};
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
    // interface cannot be used as an application-lifecycle or capability oracle.
    [[nodiscard]] os::core::Result<void> dispatch_once(
        os::ipc::Channel& channel,
        os::core::MutableByteSpan scratch) noexcept;

private:
    ShellLifecycleBackend backend_ {};
    os::ipc::PeerIdentityResolver* identity_resolver_ {nullptr};
};

} // namespace os::app
