#pragma once

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/display/compositor.hpp>
#include <os/display/shell_protocol.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/rpc.hpp>

namespace os::display {

using ShellExactActivationFn = os::core::Result<void> (*)(
    void* context,
    os::core::PeerIdentity caller,
    os::core::PeerIdentity expected_owner,
    SurfaceId application_surface) noexcept;

struct ShellCompositorBackend final {
    void* context {nullptr};
    ShellExactActivationFn activate_exact {nullptr};
};

[[nodiscard]] ShellCompositorBackend shell_compositor_backend(
    Compositor& compositor) noexcept;

// Server half remains with system.compositor. Shell client code lives in
// core/osshell so the trusted shell does not link compositor service/runtime
// implementation just to issue a bounded exact-activation request.
class ShellCompositorControlServer final {
public:
    ShellCompositorControlServer(
        ShellCompositorBackend backend,
        os::ipc::PeerIdentityResolver& identity_resolver) noexcept
        : backend_(backend), identity_resolver_(&identity_resolver) {}

    [[nodiscard]] bool valid() const noexcept {
        return backend_.activate_exact != nullptr && identity_resolver_ != nullptr;
    }

    // Handles one private activation request. Authorization precedes payload
    // validation so an ordinary process cannot use differential errors to probe
    // application ownership or generation-scoped surface existence.
    [[nodiscard]] os::core::Result<void> dispatch_once(
        os::ipc::Channel& channel,
        os::core::MutableByteSpan scratch) noexcept;

private:
    ShellCompositorBackend backend_ {};
    os::ipc::PeerIdentityResolver* identity_resolver_ {nullptr};
};

} // namespace os::display
