#pragma once

#include <cstddef>
#include <cstdint>

#include <os/app/lifecycle.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/rpc.hpp>

namespace os::app {

// Private App Manager control namespace consumed by the trusted phone shell.
// This numeric ServiceId is only a protocol label. The server authenticates the
// live caller from kernel credentials before returning lifecycle state or any
// move-only authority handle.
inline constexpr os::core::ServiceId shell_lifecycle_control_service_id{0x0000F016U};
inline constexpr std::uint32_t shell_lifecycle_operation_snapshot = 1U;
inline constexpr std::uint32_t shell_lifecycle_operation_take_compositor = 2U;

// Lightweight client half of the private shell lifecycle/bootstrap protocol.
// Keeping it in core/osapp avoids linking the trusted shell process against App
// Manager's server/runtime implementation merely to decode semantic state or
// accept a narrowly scoped move-only compositor-control capability.
class ShellLifecycleControlClient final {
public:
    explicit ShellLifecycleControlClient(os::ipc::Channel& channel) noexcept
        : connection_(channel) {}

    [[nodiscard]] os::core::Result<ApplicationLifecycleSnapshot> snapshot(
        os::core::MutableByteSpan scratch) noexcept;

    // One-time composition handoff. The returned channel is still subject to
    // the compositor control server's independent live-shell authentication;
    // possession of this descriptor alone does not mint shell authority.
    [[nodiscard]] os::core::Result<os::ipc::Channel> take_compositor_capability(
        os::core::MutableByteSpan scratch) noexcept;

private:
    os::ipc::ClientConnection connection_;
};

} // namespace os::app
