#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/display/compositor.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/rpc.hpp>

namespace os::display {

// Private shell→compositor authority namespace. This is intentionally separate
// from the application-facing compositor service operations. A numeric service
// id is only a protocol label; caller authority is always resolved from kernel
// credentials before the expected application owner or SurfaceId are parsed.
inline constexpr os::core::ServiceId shell_compositor_control_service_id{0x0000F031U};
inline constexpr std::uint32_t shell_compositor_operation_activate_exact = 1U;
inline constexpr std::size_t shell_compositor_activate_request_size_v1 = 40U;

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

class ShellCompositorControlClient final {
public:
    explicit ShellCompositorControlClient(os::ipc::Channel& channel) noexcept
        : connection_(channel) {}

    [[nodiscard]] os::core::Result<void> activate_exact(
        os::core::PeerIdentity expected_owner,
        SurfaceId application_surface,
        os::core::MutableByteSpan scratch) noexcept;

private:
    os::ipc::ClientConnection connection_;
};

} // namespace os::display
