#pragma once

#include <cstdint>

#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/core/strong_id.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/rpc.hpp>
#include <os/ipc/wire.hpp>

namespace os::app {

// M2.10 private application/runtime control protocol. It is deliberately a
// narrow trusted-runtime session, not a caller-selected daemon bus.
inline constexpr os::core::ServiceId application_service_session_id{0x0000F011U};
inline constexpr std::uint32_t application_service_session_operation_acquire = 1U;
inline constexpr std::uint32_t application_service_session_operation_acquire_input_events = 2U;
inline constexpr std::uint16_t application_service_session_version_v1 = 1U;
inline constexpr std::uint16_t application_service_session_payload_size_v1 = 16U;
inline constexpr std::uint16_t application_input_endpoint_payload_size_v1 = 4U;

// The application may report the generation it last observed. This is advisory
// state only: it never selects a service implementation, ProcessId, PrincipalId
// or authority. The trusted server always returns the currently supervised
// generation or an error.
struct ServiceAcquireRequestV1 final {
    os::ipc::WireHeaderV1 request_header {};
    os::core::ServiceId service {};
    std::uint64_t known_generation {0U};
    os::ipc::KernelPeerCredentials sender {};
};

enum class RuntimeSessionRequestKind : std::uint8_t {
    acquire_service = 1U,
    acquire_input_events = 2U,
};

struct RuntimeSessionRequestV1 final {
    os::ipc::WireHeaderV1 request_header {};
    RuntimeSessionRequestKind kind {RuntimeSessionRequestKind::acquire_service};
    os::core::ServiceId service {};
    std::uint64_t known_generation {0U};
    os::ipc::KernelPeerCredentials sender {};
};

struct PlatformServiceEndpoint final {
    os::core::ServiceId service {};
    std::uint64_t generation {0U};
    os::ipc::Channel channel {};

    PlatformServiceEndpoint() noexcept = default;
    PlatformServiceEndpoint(const PlatformServiceEndpoint&) = delete;
    PlatformServiceEndpoint& operator=(const PlatformServiceEndpoint&) = delete;
    PlatformServiceEndpoint(PlatformServiceEndpoint&&) noexcept = default;
    PlatformServiceEndpoint& operator=(PlatformServiceEndpoint&&) noexcept = default;

    [[nodiscard]] bool valid() const noexcept {
        return service.value() != 0U && generation != 0U && channel.valid();
    }
};

// Application-side session layered over the bootstrap-v2 control channel after
// READY. The channel remains long-lived for the application process lifetime.
// Returned service/event endpoints are move-only capabilities created or
// selected by trusted runtime state, never by a native descriptor in payload.
class PlatformServiceSession final {
public:
    explicit PlatformServiceSession(os::ipc::Channel& channel) noexcept
        : connection_(channel) {}

    PlatformServiceSession(const PlatformServiceSession&) = delete;
    PlatformServiceSession& operator=(const PlatformServiceSession&) = delete;

    [[nodiscard]] os::core::Result<PlatformServiceEndpoint>
    acquire(
        os::core::ServiceId service,
        std::uint64_t known_generation,
        os::core::MutableByteSpan receive_buffer) noexcept;

    // Requests a fresh private runtime→application input event channel. No
    // target identity is supplied by the application; the runtime binds the
    // endpoint to the already-authenticated application session.
    [[nodiscard]] os::core::Result<os::ipc::Channel>
    acquire_input_events(os::core::MutableByteSpan receive_buffer) noexcept;

private:
    os::ipc::ClientConnection connection_;
};

// Generic trusted runtime-side decoder used by App Manager. The sender's
// SCM_CREDENTIALS are returned for authorization against ProcessAuthority.
[[nodiscard]] os::core::Result<RuntimeSessionRequestV1>
receive_runtime_session_request(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan receive_buffer) noexcept;

// Compatibility entry point for the original service-acquisition-only server
// tests/callers. It rejects any other runtime-session operation.
[[nodiscard]] os::core::Result<ServiceAcquireRequestV1>
receive_service_acquire_request(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan receive_buffer) noexcept;

[[nodiscard]] os::core::Result<void>
send_service_acquire_response(
    os::ipc::Channel& channel,
    const os::ipc::WireHeaderV1& request_header,
    os::core::ServiceId service,
    std::uint64_t generation,
    const os::core::NativeHandle& endpoint) noexcept;

[[nodiscard]] os::core::Result<void>
send_input_event_endpoint_response(
    os::ipc::Channel& channel,
    const os::ipc::WireHeaderV1& request_header,
    const os::core::NativeHandle& endpoint) noexcept;

} // namespace os::app
