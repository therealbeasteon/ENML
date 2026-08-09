#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <os/core/identity.hpp>
#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/core/strong_id.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/wire.hpp>

namespace os::app {

inline constexpr int application_bootstrap_fd = 3;
// Legacy M2.2 Linux-private slot. Bootstrap v2 transfers typed platform-service
// endpoints through SCM_RIGHTS instead, so new applications do not accumulate
// fixed fd numbers for every service.
inline constexpr int application_storage_service_fd = 5;
inline constexpr os::core::ServiceId application_bootstrap_service_id{0x0000F010U};
inline constexpr std::uint32_t application_bootstrap_operation_initialize = 1U;
inline constexpr std::uint32_t application_bootstrap_operation_initialize_v2 = 2U;
inline constexpr std::uint16_t application_bootstrap_version_v1 = 1U;
inline constexpr std::uint16_t application_bootstrap_payload_size_v1 = 56U;
inline constexpr std::uint16_t application_bootstrap_version_v2 = 2U;
inline constexpr std::size_t max_application_service_endpoints_v2 = 4U;
inline constexpr std::size_t application_bootstrap_payload_base_size_v2 = 56U;
inline constexpr std::size_t application_bootstrap_service_entry_size_v2 = 8U;
inline constexpr std::size_t application_bootstrap_payload_max_size_v2 =
    application_bootstrap_payload_base_size_v2 +
    (max_application_service_endpoints_v2 * application_bootstrap_service_entry_size_v2);

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

// Bootstrap v2 keeps the stable application/process metadata but moves service
// connectivity into an explicit bounded `(ServiceId, transferred endpoint)`
// table. The native descriptors are transport implementation details and never
// serialized as integer fd values.
struct ApplicationBootstrapRequestV2 final {
    os::ipc::WireHeaderV1 request_header {};
    ApplicationBootstrapRecordV1 record {};
    std::array<os::core::ServiceId, max_application_service_endpoints_v2> services {};
    std::array<os::core::NativeHandle, max_application_service_endpoints_v2> endpoints {};
    std::uint16_t service_count {0U};

    ApplicationBootstrapRequestV2() noexcept = default;
    ApplicationBootstrapRequestV2(const ApplicationBootstrapRequestV2&) = delete;
    ApplicationBootstrapRequestV2& operator=(const ApplicationBootstrapRequestV2&) = delete;
    ApplicationBootstrapRequestV2(ApplicationBootstrapRequestV2&&) noexcept = default;
    ApplicationBootstrapRequestV2& operator=(ApplicationBootstrapRequestV2&&) noexcept = default;

    [[nodiscard]] os::core::Result<os::core::NativeHandle>
    take_service_endpoint(os::core::ServiceId service) noexcept;
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

[[nodiscard]] os::core::Result<void>
send_bootstrap_request_v2(
    os::ipc::Channel& channel,
    const ApplicationBootstrapRecordV1& record,
    std::span<const os::core::ServiceId> services,
    std::span<const os::core::NativeHandle> endpoints) noexcept;

[[nodiscard]] os::core::Result<ApplicationBootstrapRequestV2>
receive_bootstrap_request_v2(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan receive_buffer) noexcept;

[[nodiscard]] os::core::Result<void>
send_ready_v2(
    os::ipc::Channel& channel,
    const os::ipc::WireHeaderV1& request_header,
    const ApplicationBootstrapRecordV1& record,
    std::span<const os::core::ServiceId> services) noexcept;

[[nodiscard]] os::core::Result<void>
wait_for_ready_v2(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan receive_buffer,
    const ApplicationBootstrapRecordV1& expected,
    std::span<const os::core::ServiceId> expected_services,
    std::uint32_t timeout_ms) noexcept;

} // namespace os::app
