#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <sys/types.h>

#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/core/strong_id.hpp>
#include <os/ipc/channel.hpp>
#include <os/service/identity.hpp>
#include <os/supervisor/process_authority.hpp>
#include <os/supervisor/supervisor.hpp>

namespace os::supervisor {

inline constexpr std::size_t max_broker_services = 8U;
inline constexpr std::size_t max_broker_processes = 64U;

namespace broker_errors {
inline constexpr std::uint32_t service_not_registered = 200U;
inline constexpr std::uint32_t service_conflict = 201U;
inline constexpr std::uint32_t service_capacity = 202U;
inline constexpr std::uint32_t process_capacity = 203U;
inline constexpr std::uint32_t process_not_attached = 204U;
inline constexpr std::uint32_t service_not_attached = 205U;
inline constexpr std::uint32_t invalid_request = 206U;
} // namespace broker_errors

struct BrokeredServiceEndpoint final {
    os::core::ServiceId service {};
    std::uint64_t generation {0U};
    os::ipc::Channel channel {};

    BrokeredServiceEndpoint() noexcept = default;
    BrokeredServiceEndpoint(const BrokeredServiceEndpoint&) = delete;
    BrokeredServiceEndpoint& operator=(const BrokeredServiceEndpoint&) = delete;
    BrokeredServiceEndpoint(BrokeredServiceEndpoint&&) noexcept = default;
    BrokeredServiceEndpoint& operator=(BrokeredServiceEndpoint&&) noexcept = default;

    [[nodiscard]] bool valid() const noexcept {
        return service.value() != 0U && generation != 0U && channel.valid();
    }
};

// Trusted-system service directory / connection broker.
//
// This is intentionally not a public "connect to arbitrary daemon" API. System
// construction code registers a small fixed set of supervised platform
// services. Trusted lifecycle code then attaches one authoritative process
// identity to an explicit service set and obtains move-only service channels.
// Public request payloads never choose Linux descriptors, service executable
// paths, PrincipalIds or ProcessIds.
class ServiceBroker final {
public:
    explicit ServiceBroker(ProcessAuthority& authority) noexcept
        : authority_(&authority) {}
    ~ServiceBroker();

    ServiceBroker(const ServiceBroker&) = delete;
    ServiceBroker& operator=(const ServiceBroker&) = delete;

    [[nodiscard]] os::core::Result<void>
    register_service(os::core::ServiceId service, Supervisor& supervisor) noexcept;

    // Acquire one broker-owned authoritative identity reference and publish the
    // exact same ProcessId into every requested service. Newly-added service
    // publications are rolled back on failure; the broker keeps the base
    // identity reference until all service publications are gone.
    [[nodiscard]] os::core::Result<os::service::ProcessIdentityRecord>
    attach_process(
        pid_t native_pid,
        os::core::PrincipalId principal,
        os::core::UserId user,
        std::span<const os::core::ServiceId> services) noexcept;

    // Trusted channel acquisition. The process must already be attached to the
    // requested service. The returned channel remains generation-bound: a
    // service restart kills the old endpoint and a caller must reacquire.
    [[nodiscard]] os::core::Result<os::ipc::Channel>
    connect(os::core::ProcessId process, os::core::ServiceId service) noexcept;

    // M2.10 form of connect(): return the trusted Supervisor generation beside
    // the endpoint. The generation is observation metadata, not authority. A
    // service crash keeps the old endpoint stale permanently; callers must ask
    // again after Supervisor publishes a later generation.
    [[nodiscard]] os::core::Result<BrokeredServiceEndpoint>
    connect_current(os::core::ProcessId process, os::core::ServiceId service) noexcept;

    // Revoke every service publication owned by this broker attachment. The
    // authoritative ProcessId is released only after all publications have
    // been removed, preventing a partially-revoked identity from being reused.
    [[nodiscard]] os::core::Result<void>
    detach_process(os::core::ProcessId process) noexcept;

    [[nodiscard]] os::core::Result<os::service::ProcessIdentityRecord>
    lookup(os::core::ProcessId process) const noexcept;

    [[nodiscard]] ProcessAuthority& process_authority() noexcept { return *authority_; }
    [[nodiscard]] const ProcessAuthority& process_authority() const noexcept { return *authority_; }

    [[nodiscard]] std::size_t service_count() const noexcept;
    [[nodiscard]] std::size_t process_count() const noexcept;

private:
    struct ServiceSlot final {
        bool occupied {false};
        os::core::ServiceId id {};
        Supervisor* supervisor {nullptr};
    };

    struct ProcessSlot final {
        bool occupied {false};
        os::service::ProcessIdentityRecord record {};
        std::array<bool, max_broker_services> published {};
    };

    ProcessAuthority* authority_ {nullptr};
    std::array<ServiceSlot, max_broker_services> services_ {};
    std::array<ProcessSlot, max_broker_processes> processes_ {};

    [[nodiscard]] ServiceSlot* find_service(os::core::ServiceId service) noexcept;
    [[nodiscard]] const ServiceSlot* find_service(os::core::ServiceId service) const noexcept;
    [[nodiscard]] std::size_t service_index(const ServiceSlot* slot) const noexcept;
    [[nodiscard]] ProcessSlot* find_process(os::core::ProcessId process) noexcept;
    [[nodiscard]] const ProcessSlot* find_process(os::core::ProcessId process) const noexcept;
    [[nodiscard]] ProcessSlot* find_native_pid(pid_t native_pid) noexcept;
};

} // namespace os::supervisor
