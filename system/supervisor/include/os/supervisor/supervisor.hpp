#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <sys/types.h>

#include <os/core/identity.hpp>
#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/core/strong_id.hpp>
#include <os/ipc/channel.hpp>
#include <os/service/identity.hpp>
#include <os/sandbox/sandbox.hpp>

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
};

struct ServiceStatus final {
    ServiceState state {ServiceState::stopped};
    os::core::PeerIdentity identity {};
    pid_t native_pid {-1};
    std::uint64_t generation {0};
    std::uint32_t restarts_in_window {0};
};

// M0.7 single-service supervisor prototype. In addition to service lifecycle,
// it owns the authoritative mapping from native Linux processes to EMNL
// ProcessId/PrincipalId/UserId identities. Services receive only validated
// mappings over the private control channel.
class Supervisor final {
public:
    explicit Supervisor(ServiceLaunchConfig config) noexcept;
    ~Supervisor();

    Supervisor(const Supervisor&) = delete;
    Supervisor& operator=(const Supervisor&) = delete;

    [[nodiscard]] os::core::Result<void> start() noexcept;
    [[nodiscard]] os::core::Result<void> maintain() noexcept;
    [[nodiscard]] os::core::Result<os::ipc::Channel> connect() noexcept;

    // Registers an already-existing process under a supervisor-assigned
    // ProcessId. Re-registering the same live PID with the same principal/user
    // re-publishes the existing identity to a newly restarted service.
    [[nodiscard]] os::core::Result<os::service::ProcessIdentityRecord>
    register_process(
        pid_t native_pid,
        os::core::PrincipalId principal,
        os::core::UserId user) noexcept;

    [[nodiscard]] os::core::Result<void>
    unregister_process(os::core::ProcessId process_id) noexcept;

    [[nodiscard]] os::core::Result<os::service::ProcessIdentityRecord>
    lookup_process(pid_t native_pid) const noexcept;

    [[nodiscard]] ServiceStatus status() const noexcept;

    // Test/shutdown mechanism only. Normal service failure observation still
    // happens through maintain()/waitpid on the exact child.
    [[nodiscard]] os::core::Result<void> terminate(int signal_number) noexcept;

private:
    struct ProcessEntry final {
        bool occupied {false};
        bool managed_service {false};
        os::service::ProcessIdentityRecord record {};
        os::core::NativeHandle pidfd {};
    };

    ServiceLaunchConfig config_ {};
    ServiceState state_ {ServiceState::stopped};
    os::core::PeerIdentity service_identity_ {};
    pid_t child_pid_ {-1};
    std::uint64_t generation_ {0};
    std::uint64_t next_process_id_ {1};
    std::uint64_t next_identity_request_id_ {2};
    std::uint64_t next_restart_at_ms_ {0};

    os::ipc::Channel control_ {};
    os::ipc::Channel client_endpoint_ {};

    static constexpr std::size_t max_restart_history = 8U;
    std::uint64_t restart_history_[max_restart_history] {};
    std::size_t restart_history_count_ {0};

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
