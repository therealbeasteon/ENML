#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/display/buffer.hpp>
#include <os/display/compositor.hpp>
#include <os/display/input_bridge.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/rpc.hpp>

namespace os::display {

inline constexpr os::core::ServiceId compositor_service_id{0x0000F030U};

// Stable supervised principal for the privileged input service. Knowledge of
// this value is not authority: compositor RPC resolves the kernel-authenticated
// PeerIdentity and requires its principal to match before exposing scene hit
// information.
inline constexpr os::core::PrincipalId input_service_principal{
    0x454E4D4C494E5055ULL,
    0x5400000000000001ULL,
};

inline constexpr std::uint32_t compositor_op_get_configuration = 1U;
inline constexpr std::uint32_t compositor_op_create_surface = 2U;
inline constexpr std::uint32_t compositor_op_destroy_surface = 3U;
inline constexpr std::uint32_t compositor_op_set_bounds = 4U;
inline constexpr std::uint32_t compositor_op_set_visibility = 5U;
inline constexpr std::uint32_t compositor_op_activate_application = 6U;
inline constexpr std::uint32_t compositor_op_allocate_buffer = 7U;
inline constexpr std::uint32_t compositor_op_release_buffer = 8U;
inline constexpr std::uint32_t compositor_op_submit_frame = 9U;
inline constexpr std::uint32_t compositor_op_input_hit_test = 10U;
inline constexpr std::uint32_t compositor_op_input_validate = 11U;

inline constexpr std::size_t max_compositor_clients = 16U;

class CompositorClient final {
public:
    explicit CompositorClient(os::ipc::ClientConnection& connection) noexcept
        : connection_(&connection) {}

    [[nodiscard]] os::core::Result<DisplayConfiguration> configuration(
        os::core::MutableByteSpan scratch) noexcept;
    [[nodiscard]] os::core::Result<SurfaceDescriptor> create_surface(
        const CreateSurfaceRequest& request,
        os::core::MutableByteSpan scratch) noexcept;
    [[nodiscard]] os::core::Result<void> destroy_surface(
        SurfaceId surface,
        os::core::MutableByteSpan scratch) noexcept;
    [[nodiscard]] os::core::Result<void> set_bounds(
        SurfaceId surface,
        Rect bounds,
        os::core::MutableByteSpan scratch) noexcept;
    [[nodiscard]] os::core::Result<void> set_visibility(
        SurfaceId surface,
        SurfaceVisibility visibility,
        os::core::MutableByteSpan scratch) noexcept;
    [[nodiscard]] os::core::Result<void> activate_application(
        SurfaceId surface,
        os::core::MutableByteSpan scratch) noexcept;
    [[nodiscard]] os::core::Result<SharedBufferLease> allocate_buffer(
        PixelSize size,
        PixelFormat format,
        os::core::MutableByteSpan scratch) noexcept;
    [[nodiscard]] os::core::Result<void> release_buffer(
        BufferId buffer,
        os::core::MutableByteSpan scratch) noexcept;
    [[nodiscard]] os::core::Result<FrameReceipt> submit_frame(
        const FrameSubmission& submission,
        os::core::MutableByteSpan scratch) noexcept;

private:
    os::ipc::ClientConnection* connection_ {nullptr};
};

// Privileged facade for the supervised input service. The C++ type itself is
// not a capability. Every call traverses the normal authenticated compositor
// RPC path and is authorized against input_service_principal on the server.
class InputCompositorClient final {
public:
    explicit InputCompositorClient(os::ipc::ClientConnection& connection) noexcept
        : connection_(&connection) {}

    [[nodiscard]] os::core::Result<SurfaceInputHit> hit_test(
        std::int32_t global_x,
        std::int32_t global_y,
        os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<void> validate_before_delivery(
        const SurfaceInputHit& hit,
        os::core::MutableByteSpan scratch) noexcept;

private:
    os::ipc::ClientConnection* connection_ {nullptr};
};

class CompositorService final {
public:
    CompositorService(
        Compositor& compositor,
        SharedBufferPool& buffers,
        os::ipc::PeerIdentityResolver& identity_resolver,
        os::core::PrincipalId trusted_input_principal = {}) noexcept
        : compositor_(&compositor),
          buffers_(&buffers),
          identity_resolver_(&identity_resolver),
          input_authority_(compositor, trusted_input_principal) {}

    [[nodiscard]] os::core::Result<void> dispatch_once(
        os::ipc::Channel& channel,
        os::core::MutableByteSpan scratch) noexcept;

    void prune_dead_clients() noexcept;
    void revoke_process(os::core::ProcessId process) noexcept;

private:
    struct ClientEntry final {
        bool occupied {false};
        os::core::PeerIdentity peer {};
        os::ipc::KernelPeerCredentials kernel {};
        os::core::NativeHandle pidfd {};
    };

    [[nodiscard]] os::core::Result<void> note_client(
        os::core::PeerIdentity peer,
        os::ipc::KernelPeerCredentials kernel) noexcept;

    Compositor* compositor_ {nullptr};
    SharedBufferPool* buffers_ {nullptr};
    os::ipc::PeerIdentityResolver* identity_resolver_ {nullptr};
    InputBridgeAuthority input_authority_;
    std::array<ClientEntry, max_compositor_clients> clients_ {};
};

} // namespace os::display
