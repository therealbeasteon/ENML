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
// live caller from kernel credentials before returning lifecycle state.
inline constexpr os::core::ServiceId shell_lifecycle_control_service_id{0x0000F016U};
inline constexpr std::uint32_t shell_lifecycle_operation_snapshot = 1U;

// Lightweight client half of the private shell lifecycle protocol. Keeping it
// in core/osapp avoids linking the trusted shell process against App Manager's
// server/runtime implementation merely to decode a bounded semantic snapshot.
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
