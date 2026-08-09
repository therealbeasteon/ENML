#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/display/buffer.hpp>
#include <os/display/compositor.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/rpc.hpp>

namespace os::display {

inline constexpr os::core::ServiceId compositor_service_id{0x0000F030U};

inline constexpr std::uint32_t compositor_op_get_configuration = 1U;
inline constexpr std::uint32_t compositor_op_create_surface = 2U;
inline constexpr std::uint32_t compositor_op_destroy_surface = 3U;
inline constexpr std::uint32_t compositor_op_set_bounds = 4U;
inline constexpr std::uint32_t compositor_op_set_visibility = 5U;
inline constexpr std::uint32_t compositor_op_activate_application = 6U;
inline constexpr std::uint32_t compositor_op_allocate_buffer = 7U;
inline constexpr std::uint32_t compositor_op_release_buffer = 8U;
inline constexpr std::uint32_t compositor_op_submit_frame = 9U;

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

class CompositorService final {
public:
    CompositorService(
        Compositor& compositor,
        SharedBufferPool& buffers,
        os::ipc::PeerIdentityResolver& identity_resolver) noexcept
        : compositor_(&compositor), buffers_(&buffers), identity_resolver_(&identity_resolver) {}

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
    std::array<ClientEntry, max_compositor_clients> clients_ {};
};

} // namespace os::display
