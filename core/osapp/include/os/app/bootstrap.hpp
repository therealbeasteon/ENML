#pragma once

#include <cstdint>

#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/core/strong_id.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/wire.hpp>

namespace os::app {

inline constexpr int application_bootstrap_fd = 3;
// Linux-private bootstrap transport slot. M2.2 carries a connected Storage
// Service endpoint here; applications must consume it through the ENML storage
// API rather than treating descriptor numbers as stable public ABI.
inline constexpr int application_storage_service_fd = 5;
inline constexpr os::core::ServiceId application_bootstrap_service_id{0x0000F010U};
inline constexpr std::uint32_t application_bootstrap_operation_initialize = 1U;
inline constexpr std::uint16_t application_bootstrap_version_v1 = 1U;
inline constexpr std::uint16_t application_bootstrap_payload_size_v1 = 56U;

struct ApplicationBootstrapRecordV1 final {
    os::core::ApplicationInstanceId instance {};
    os::core::PeerIdentity identity {};
    std::uint64_t package_generation {0U};

    [[nodiscard]] friend constexpr bool
    operator==(const ApplicationBootstrapRecordV1&, const ApplicationBootstrapRecordV1&) = default;
};

struct ApplicationBootstrapRequest final {
    os::ipc::WireHeaderV1 request_header {};
    ApplicationBootstrapRecordV1 record {};
};

[[nodiscard]] os::core::Result<void>
send_bootstrap_request(
    os::ipc::Channel& channel,
    const ApplicationBootstrapRecordV1& record) noexcept;

[[nodiscard]] os::core::Result<ApplicationBootstrapRequest>
receive_bootstrap_request(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan receive_buffer) noexcept;

[[nodiscard]] os::core::Result<void>
send_ready(
    os::ipc::Channel& channel,
    const os::ipc::WireHeaderV1& request_header,
    const ApplicationBootstrapRecordV1& record) noexcept;

[[nodiscard]] os::core::Result<void>
wait_for_ready(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan receive_buffer,
    const ApplicationBootstrapRecordV1& expected,
    std::uint32_t timeout_ms) noexcept;

} // namespace os::app
