#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <sys/types.h>

#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/core/strong_id.hpp>
#include <os/ipc/channel.hpp>
#include <os/sandbox/sandbox.hpp>
#include <os/service/identity.hpp>
#include <os/supervisor/process_authority.hpp>

namespace os::supervisor {

enum class RestartPolicy : std::uint8_t {
    never,
    on_failure,
};

enum class ServiceState : std::uint8_t {
    stopped,
    starting,
    running,
    restart_wait,
    crash_loop,
};

struct ServiceDescriptorV1 final {
    os::core::ServiceId service_id {};
    os::core::PrincipalId principal_id {};
    os::core::UserId user_id {};
    const char* name {nullptr};
    RestartPolicy restart_policy {RestartPolicy::on_failure};
    std::uint32_t restart_delay_ms {25};
    std::uint32_t max_restarts_in_window {3};
    std::uint32_t restart_window_ms {2000};
    std::uint32_t readiness_timeout_ms {1000};
    os::sandbox::SandboxPolicyV1 sandbox {};
};

struct ServiceLaunchConfig final {
    ServiceDescriptorV1 descriptor {};
    const char* executable_path {nullptr};
    // Borrowed trusted directory handle retained by the launcher across service
    // restarts. When >=0, the child receives only a duplicate at private fd 5.
    // This is an internal service-construction mechanism, not application ABI.
    int private_state_directory_fd {-1};
};

struct ServiceStatus final {
    ServiceState state {ServiceState::stopped};
    os::core::PeerIdentity identity {};
    pid_t native_pid {-1};
    std::uint64_t generation {0};
    std::uint32_t restarts_in_window {0};
};

// Single-service lifecycle supervisor. M2.9 separates boot-scoped process
// identity allocation from individual service instances: legacy construction
// uses a private authority, while several supervisors may explicitly share one
// ProcessAuthority to publish the same PeerIdentity into multiple services.
class Supervisor final {
public:
    explicit Supervisor(ServiceLaunchConfig config) noexcept;
    Supervisor(ServiceLaunchConfig config, ProcessAuthority& authority) noexcept;
    ~Supervisor();

    Supervisor(const Supervisor&) = delete;
    Supervisor& operator=(const Supervisor&) = delete;

    [[nodiscard]] os::core::Result<void> start() noexcept;
    [[nodiscard]] os::core::Result<void> maintain() noexcept;
    [[nodiscard]] os::core::Result<os::ipc::Channel> connect() noexcept;

    // Trusted-system escape hatch for service-specific control extensions.
    // This duplicates the supervisor side of the private bootstrap/control
    // channel; applications must never receive it. M2 remains synchronous, so
    // callers must not race control requests with Supervisor operations.
    [[nodiscard]] os::core::Result<os::ipc::Channel>
    connect_private_control() noexcept;

    // Registers an already-existing process and publishes its authoritative
    // identity to this service. With a shared ProcessAuthority, another
    // Supervisor registering the same live PID/principal/user receives the same
    // logical ProcessId rather than allocating a service-local identity.
    [[nodiscard]] os::core::Result<os::service::ProcessIdentityRecord>
    register_process(
        pid_t native_pid,
        os::core::PrincipalId principal,
        os::core::UserId user) noexcept;

    // Removes this service's publication reference. A shared authoritative
    // identity remains live while another Supervisor still references it.
    [[nodiscard]] os::core::Result<void>
    unregister_process(os::core::ProcessId process_id) noexcept;

    [[nodiscard]] os::core::Result<os::service::ProcessIdentityRecord>
    lookup_process(pid_t native_pid) const noexcept;

    [[nodiscard]] ProcessAuthority& process_authority() noexcept { return *authority_; }
    [[nodiscard]] const ProcessAuthority& process_authority() const noexcept { return *authority_; }

    [[nodiscard]] ServiceStatus status() const noexcept;

    // Test/shutdown mechanism only. Normal service failure observation still
    // happens through maintain()/waitpid on the exact child.
    [[nodiscard]] os::core::Result<void> terminate(int signal_number) noexcept;

private:
    struct ProcessEntry final {
        bool occupied {false};
        bool managed_service {false};
        os::service::ProcessIdentityRecord record {};
    };

    ServiceLaunchConfig config_ {};
    ProcessAuthority owned_authority_ {};
    ProcessAuthority* authority_ {&owned_authority_};
    ServiceState state_ {ServiceState::stopped};
    os::core::PeerIdentity service_identity_ {};
    pid_t child_pid_ {-1};
    std::uint64_t generation_ {0};
    std::uint64_t next_identity_request_id_ {2};
    std::uint64_t next_restart_at_ms_ {0};

    os::ipc::Channel control_ {};
    os::ipc::Channel client_endpoint_ {};

    static constexpr std::size_t max_restart_history = 8U;
    std::uint64_t restart_history_[max_restart_history] {};
    std::size_t restart_history_count_ {0};

    // Service-local publication set. ProcessId allocation/liveness belongs to
    // ProcessAuthority; this table only records which identities must be
    // present in this service's generation-local IdentityRegistry.
    static constexpr std::size_t max_process_entries = 64U;
    std::array<ProcessEntry, max_process_entries> process_entries_ {};

    [[nodiscard]] os::core::Result<void> spawn_service() noexcept;
    void observe_exit(int status) noexcept;
    [[nodiscard]] bool restart_budget_available(std::uint64_t now_ms) noexcept;
    void record_restart(std::uint64_t now_ms) noexcept;
    void close_runtime_channels() noexcept;

    [[nodiscard]] os::core::Result<os::service::ProcessIdentityRecord>
    create_process_record(
        pid_t native_pid,
        os::core::PrincipalId principal,
        os::core::UserId user,
        bool managed_service) noexcept;

    [[nodiscard]] os::core::Result<void>
    publish_process_to_service(ProcessEntry& entry) noexcept;

    [[nodiscard]] ProcessEntry* find_process_by_pid(pid_t native_pid) noexcept;
    [[nodiscard]] const ProcessEntry* find_process_by_pid(pid_t native_pid) const noexcept;
    [[nodiscard]] ProcessEntry* find_process_by_id(os::core::ProcessId process_id) noexcept;
    void remove_process_entry(os::core::ProcessId process_id) noexcept;
    void prune_dead_processes() noexcept;
};

} // namespace os::supervisor
