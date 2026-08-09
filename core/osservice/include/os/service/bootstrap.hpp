#pragma once

#include <cstdint>

#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/core/strong_id.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/wire.hpp>

namespace os::service {

// Private supervisor/service bootstrap protocol. This is not an application
// ABI and may evolve independently while M0 is pre-freeze.
inline constexpr os::core::ServiceId bootstrap_service_id{0xFFFF0001U};
inline constexpr std::uint32_t bootstrap_operation_initialize = 1U;
inline constexpr int bootstrap_control_fd = 3;
inline constexpr int service_endpoint_fd = 4;
// Optional trusted service-private state directory. The Supervisor installs it
// only when ServiceLaunchConfig carries an already-authorized directory fd.
// Applications never inherit this descriptor through the public service path.
inline constexpr int service_state_directory_fd = 5;

struct BootstrapRecordV1 final {
    os::core::PeerIdentity identity {};
    os::core::ServiceId service_id {};
    std::uint64_t boot_generation {0};
};

struct BootstrapRequest final {
    os::ipc::WireHeaderV1 request_header {};
    BootstrapRecordV1 record {};
};

[[nodiscard]] os::core::Result<void>
send_bootstrap_request(
    os::ipc::Channel& channel,
    const BootstrapRecordV1& record) noexcept;

[[nodiscard]] os::core::Result<void>
wait_for_ready(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan receive_buffer,
    std::uint32_t timeout_ms) noexcept;

[[nodiscard]] os::core::Result<BootstrapRequest>
receive_bootstrap_request(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan receive_buffer,
    os::core::ServiceId expected_service) noexcept;

[[nodiscard]] os::core::Result<void>
send_ready(
    os::ipc::Channel& channel,
    const os::ipc::WireHeaderV1& request_header) noexcept;

} // namespace os::service
