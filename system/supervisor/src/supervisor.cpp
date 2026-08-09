#include <os/supervisor/supervisor.hpp>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <utility>

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/service/bootstrap.hpp>
#include <os/service/identity.hpp>
#include <os/sandbox/sandbox.hpp>

namespace os::supervisor {
namespace {

[[nodiscard]] constexpr os::core::Error service_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

[[nodiscard]] constexpr os::core::Error security_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::security, code);
}

[[nodiscard]] std::uint64_t monotonic_milliseconds() noexcept {
    timespec value{};
    if (::clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return 0U;
    }
    const auto seconds = static_cast<std::uint64_t>(value.tv_sec);
    const auto nanoseconds = static_cast<std::uint64_t>(value.tv_nsec);
    return (seconds * 1000U) + (nanoseconds / 1000000U);
}

[[nodiscard]] int duplicate_cloexec(int fd) noexcept {
    int result = -1;
    do {
        result = ::fcntl(fd, F_DUPFD_CLOEXEC, 10);
    } while (result < 0 && errno == EINTR);
    return result;
}

void close_extra_descriptors(unsigned first_fd) noexcept {
#if defined(SYS_close_range)
    if (::syscall(SYS_close_range, first_fd, ~0U, 0U) == 0) {
        return;
    }
#endif
    long limit = ::sysconf(_SC_OPEN_MAX);
    if (limit < 0 || limit > 4096) {
        limit = 4096;
    }
    for (int fd = static_cast<int>(first_fd); fd < static_cast<int>(limit); ++fd) {
        (void)::close(fd);
    }
}

[[noreturn]] void exec_service_child(
    const char* executable_path,
    int control_fd,
    int service_fd,
    int state_directory_fd,
    const os::sandbox::SandboxPolicyV1& sandbox_policy) noexcept {
    const int control_temp = duplicate_cloexec(control_fd);
    const int service_temp = duplicate_cloexec(service_fd);
    const int state_temp = state_directory_fd >= 0
        ? duplicate_cloexec(state_directory_fd)
        : -1;
    if (control_temp < 0 || service_temp < 0 ||
        (state_directory_fd >= 0 && state_temp < 0)) {
        std::_Exit(120);
    }

    if (::dup2(control_temp, os::service::bootstrap_control_fd) < 0 ||
        ::dup2(service_temp, os::service::service_endpoint_fd) < 0) {
        std::_Exit(121);
    }
    if (state_directory_fd >= 0 &&
        ::dup2(state_temp, os::service::service_state_directory_fd) < 0) {
        std::_Exit(121);
    }

    (void)::close(control_temp);
    (void)::close(service_temp);
    if (state_temp >= 0) (void)::close(state_temp);

    if (state_directory_fd >= 0) {
        close_extra_descriptors(
            static_cast<unsigned>(os::service::service_state_directory_fd + 1));
    } else {
        (void)::close(os::service::service_state_directory_fd);
        close_extra_descriptors(
            static_cast<unsigned>(os::service::service_state_directory_fd));
    }

    auto sandbox_result = os::sandbox::apply_before_exec(executable_path, sandbox_policy);
    if (!sandbox_result) {
        std::_Exit(122);
    }

    // Deliberately fixed environment: no inherited LD_PRELOAD, language,
    // credentials, tokens, or application-controlled authority.
    char path_env[] = "PATH=/usr/bin:/bin";
    char lang_env[] = "LANG=C";
    char* const environment[] = {path_env, lang_env, nullptr};
    char* const arguments[] = {const_cast<char*>(executable_path), nullptr};
    ::execve(executable_path, arguments, environment);
    std::_Exit(127);
}

} // namespace

Supervisor::Supervisor(ServiceLaunchConfig config) noexcept
    : config_(config) {}

Supervisor::Supervisor(ServiceLaunchConfig config, ProcessAuthority& authority) noexcept
    : config_(config), authority_(&authority) {}

Supervisor::~Supervisor() {
    if (child_pid_ > 0) {
        (void)::kill(child_pid_, SIGTERM);
        int status = 0;
        while (::waitpid(child_pid_, &status, 0) < 0 && errno == EINTR) {}
    }

    // A shared ProcessAuthority outlives an individual service supervisor. Drop
    // every publication reference owned by this Supervisor so destroying one
    // service cannot leak or accidentally retain global process authority.
    for (auto& entry : process_entries_) {
        if (!entry.occupied) continue;
        os::core::discard_result(authority_->release(entry.record.peer.process));
        entry = ProcessEntry{};
    }
    close_runtime_channels();
}

os::core::Result<void> Supervisor::start() noexcept {
    if (state_ != ServiceState::stopped) {
        return service_error(os::core::errors::service::invalid_request);
    }
    if (config_.executable_path == nullptr || config_.descriptor.name == nullptr ||
        config_.descriptor.service_id.value() == 0U ||
        !os::core::valid_principal(config_.descriptor.principal_id)) {
        return service_error(os::core::errors::service::launch_failed);
    }
    if (config_.private_state_directory_fd >= 0) {
        struct stat metadata {};
        if (::fstat(config_.private_state_directory_fd, &metadata) != 0 ||
            !S_ISDIR(metadata.st_mode)) {
            return service_error(os::core::errors::service::launch_failed);
        }
    }
    return spawn_service();
}

Supervisor::ProcessEntry* Supervisor::find_process_by_pid(pid_t native_pid) noexcept {
    for (auto& entry : process_entries_) {
        if (entry.occupied &&
            entry.record.kernel.process_id == static_cast<std::int64_t>(native_pid)) {
            return &entry;
        }
    }
    return nullptr;
}

const Supervisor::ProcessEntry* Supervisor::find_process_by_pid(pid_t native_pid) const noexcept {
    for (const auto& entry : process_entries_) {
        if (entry.occupied &&
            entry.record.kernel.process_id == static_cast<std::int64_t>(native_pid)) {
            return &entry;
        }
    }
    return nullptr;
}

Supervisor::ProcessEntry* Supervisor::find_process_by_id(os::core::ProcessId process_id) noexcept {
    for (auto& entry : process_entries_) {
        if (entry.occupied && entry.record.peer.process == process_id) return &entry;
    }
    return nullptr;
}

void Supervisor::remove_process_entry(os::core::ProcessId process_id) noexcept {
    for (auto& entry : process_entries_) {
        if (entry.occupied && entry.record.peer.process == process_id) {
            os::core::discard_result(authority_->release(process_id));
            entry = ProcessEntry{};
            return;
        }
    }
}

os::core::Result<os::service::ProcessIdentityRecord>
Supervisor::create_process_record(
    pid_t native_pid,
    os::core::PrincipalId principal,
    os::core::UserId user,
    bool managed_service) noexcept {
    if (native_pid <= 0 || !os::core::valid_principal(principal)) {
        return security_error(os::core::errors::security::invalid_identity);
    }
    if (find_process_by_pid(native_pid) != nullptr) {
        return security_error(os::core::errors::security::credential_mismatch);
    }

    ProcessEntry* free_entry = nullptr;
    for (auto& entry : process_entries_) {
        if (!entry.occupied) {
            free_entry = &entry;
            break;
        }
    }
    if (free_entry == nullptr) {
        return security_error(os::core::errors::security::registry_full);
    }

    auto authoritative = authority_->acquire(native_pid, principal, user);
    if (!authoritative) return authoritative.error();

    *free_entry = ProcessEntry{
        .occupied = true,
        .managed_service = managed_service,
        .record = authoritative.value(),
    };
    return authoritative.value();
}

os::core::Result<void> Supervisor::publish_process_to_service(ProcessEntry& entry) noexcept {
    if (!entry.occupied || !control_.valid()) {
        return security_error(os::core::errors::security::invalid_identity);
    }
    if (next_identity_request_id_ == 0U) {
        return os::core::make_error(
            os::core::ErrorDomain::ipc,
            os::ipc::errors::request_id_exhausted);
    }

    auto pidfd = authority_->duplicate_pidfd(entry.record.peer.process);
    if (!pidfd) return pidfd.error();

    const auto request_id = os::core::RequestId{next_identity_request_id_};
    if (next_identity_request_id_ == std::numeric_limits<std::uint64_t>::max()) {
        next_identity_request_id_ = 0U;
    } else {
        ++next_identity_request_id_;
    }
    return os::service::register_identity_with_service(
        control_,
        entry.record,
        pidfd.value(),
        request_id,
        config_.descriptor.readiness_timeout_ms);
}

os::core::Result<void> Supervisor::spawn_service() noexcept {
    state_ = ServiceState::starting;
    close_runtime_channels();

    auto control_pair_result = os::ipc::Channel::create_local_pair();
    if (!control_pair_result) {
        state_ = ServiceState::stopped;
        return control_pair_result.error();
    }
    auto control_pair = std::move(control_pair_result).value();

    auto service_pair_result = os::ipc::Channel::create_local_pair();
    if (!service_pair_result) {
        state_ = ServiceState::stopped;
        return service_pair_result.error();
    }
    auto service_pair = std::move(service_pair_result).value();

    const auto new_generation = generation_ + 1U;

    const pid_t child = ::fork();
    if (child < 0) {
        state_ = ServiceState::stopped;
        return service_error(os::core::errors::service::launch_failed);
    }

    if (child == 0) {
        control_pair[0].close();
        service_pair[0].close();
        exec_service_child(
            config_.executable_path,
            control_pair[1].native_fd(),
            service_pair[1].native_fd(),
            config_.private_state_directory_fd,
            config_.descriptor.sandbox);
    }

    control_pair[1].close();
    service_pair[1].close();
    control_ = std::move(control_pair[0]);
    client_endpoint_ = std::move(service_pair[0]);
    child_pid_ = child;
    generation_ = new_generation;

    auto service_record_result = create_process_record(
        child_pid_, config_.descriptor.principal_id, config_.descriptor.user_id, true);
    if (!service_record_result) {
        (void)::kill(child_pid_, SIGKILL);
        int status = 0;
        while (::waitpid(child_pid_, &status, 0) < 0 && errno == EINTR) {}
        child_pid_ = -1;
        close_runtime_channels();
        state_ = ServiceState::stopped;
        return service_record_result.error();
    }
    const auto service_record = service_record_result.value();
    service_identity_ = service_record.peer;

    const os::service::BootstrapRecordV1 bootstrap{
        .identity = service_identity_,
        .service_id = config_.descriptor.service_id,
        .boot_generation = 1U,
    };
    auto send_result = os::service::send_bootstrap_request(control_, bootstrap);
    if (!send_result) {
        (void)::kill(child_pid_, SIGKILL);
        int status = 0;
        while (::waitpid(child_pid_, &status, 0) < 0 && errno == EINTR) {}
        remove_process_entry(service_identity_.process);
        service_identity_ = {};
        child_pid_ = -1;
        close_runtime_channels();
        state_ = ServiceState::stopped;
        return send_result.error();
    }

    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer{};
    auto ready_result = os::service::wait_for_ready(
        control_, receive_buffer, config_.descriptor.readiness_timeout_ms);
    if (!ready_result) {
        (void)::kill(child_pid_, SIGKILL);
        int status = 0;
        while (::waitpid(child_pid_, &status, 0) < 0 && errno == EINTR) {}
        remove_process_entry(service_identity_.process);
        service_identity_ = {};
        child_pid_ = -1;
        close_runtime_channels();
        state_ = ServiceState::stopped;
        return ready_result.error();
    }

    // A restarted service has a fresh in-process registry. Republish every
    // still-live external process identity that this Supervisor owns. The
    // ProcessId itself remains allocated by the shared boot-scoped authority.
    for (auto& entry : process_entries_) {
        if (!entry.occupied || entry.managed_service) continue;
        auto live = authority_->lookup(entry.record.peer.process);
        if (!live) {
            const auto stale_process = entry.record.peer.process;
            remove_process_entry(stale_process);
            continue;
        }
        auto publish_result = publish_process_to_service(entry);
        if (!publish_result) {
            (void)::kill(child_pid_, SIGKILL);
            int status = 0;
            while (::waitpid(child_pid_, &status, 0) < 0 && errno == EINTR) {}
            remove_process_entry(service_identity_.process);
            service_identity_ = {};
            child_pid_ = -1;
            close_runtime_channels();
            state_ = ServiceState::stopped;
            return publish_result.error();
        }
    }

    state_ = ServiceState::running;
    return {};
}

bool Supervisor::restart_budget_available(std::uint64_t now_ms) noexcept {
    std::size_t write = 0U;
    for (std::size_t index = 0; index < restart_history_count_; ++index) {
        const auto age = now_ms >= restart_history_[index]
            ? now_ms - restart_history_[index]
            : 0U;
        if (age <= config_.descriptor.restart_window_ms) {
            restart_history_[write++] = restart_history_[index];
        }
    }
    restart_history_count_ = write;
    return restart_history_count_ < config_.descriptor.max_restarts_in_window;
}

void Supervisor::record_restart(std::uint64_t now_ms) noexcept {
    if (restart_history_count_ < max_restart_history) {
        restart_history_[restart_history_count_++] = now_ms;
        return;
    }
    for (std::size_t index = 1U; index < max_restart_history; ++index) {
        restart_history_[index - 1U] = restart_history_[index];
    }
    restart_history_[max_restart_history - 1U] = now_ms;
}

void Supervisor::observe_exit(int status) noexcept {
    const bool failed = !WIFEXITED(status) || WEXITSTATUS(status) != 0;
    if (service_identity_.process.value() != 0U) {
        remove_process_entry(service_identity_.process);
    }
    service_identity_ = {};
    child_pid_ = -1;
    control_.close();
    client_endpoint_.close();

    if (!failed || config_.descriptor.restart_policy == RestartPolicy::never) {
        state_ = ServiceState::stopped;
        return;
    }

    const auto now = monotonic_milliseconds();
    if (!restart_budget_available(now)) {
        state_ = ServiceState::crash_loop;
        return;
    }

    record_restart(now);
    next_restart_at_ms_ = now + config_.descriptor.restart_delay_ms;
    state_ = ServiceState::restart_wait;
}

void Supervisor::prune_dead_processes() noexcept {
    for (auto& entry : process_entries_) {
        if (!entry.occupied || entry.managed_service) continue;
        auto live = authority_->lookup(entry.record.peer.process);
        if (!live) {
            const auto stale_process = entry.record.peer.process;
            remove_process_entry(stale_process);
        }
    }
}

os::core::Result<void> Supervisor::maintain() noexcept {
    prune_dead_processes();

    if (child_pid_ > 0) {
        int status = 0;
        pid_t result = -1;
        do {
            result = ::waitpid(child_pid_, &status, WNOHANG);
        } while (result < 0 && errno == EINTR);

        if (result < 0) {
            return service_error(os::core::errors::service::launch_failed);
        }
        if (result == child_pid_) {
            observe_exit(status);
        }
    }

    if (state_ == ServiceState::restart_wait) {
        const auto now = monotonic_milliseconds();
        if (now >= next_restart_at_ms_) {
            auto restart_result = spawn_service();
            if (!restart_result) {
                const auto failed_at = monotonic_milliseconds();
                if (!restart_budget_available(failed_at)) {
                    state_ = ServiceState::crash_loop;
                    return service_error(os::core::errors::service::crash_loop);
                }
                record_restart(failed_at);
                next_restart_at_ms_ = failed_at + config_.descriptor.restart_delay_ms;
                state_ = ServiceState::restart_wait;
            }
        }
    }

    if (state_ == ServiceState::crash_loop) {
        return service_error(os::core::errors::service::crash_loop);
    }
    return {};
}

os::core::Result<os::service::ProcessIdentityRecord>
Supervisor::register_process(
    pid_t native_pid,
    os::core::PrincipalId principal,
    os::core::UserId user) noexcept {
    if (state_ != ServiceState::running || !control_.valid()) {
        return service_error(os::core::errors::service::not_running);
    }
    if (!os::core::valid_principal(principal)) {
        return security_error(os::core::errors::security::invalid_identity);
    }

    if (auto* existing = find_process_by_pid(native_pid); existing != nullptr) {
        if (existing->managed_service || existing->record.peer.principal != principal ||
            existing->record.peer.user != user) {
            return security_error(os::core::errors::security::credential_mismatch);
        }
        auto authoritative = authority_->lookup(existing->record.peer.process);
        if (authoritative) {
            auto publish_result = publish_process_to_service(*existing);
            if (!publish_result) return publish_result.error();
            return existing->record;
        }

        const auto stale_process = existing->record.peer.process;
        remove_process_entry(stale_process);
    }

    auto record_result = create_process_record(native_pid, principal, user, false);
    if (!record_result) return record_result.error();
    auto* entry = find_process_by_id(record_result.value().peer.process);
    if (entry == nullptr) {
        return security_error(os::core::errors::security::invalid_identity);
    }
    auto publish_result = publish_process_to_service(*entry);
    if (!publish_result) {
        remove_process_entry(entry->record.peer.process);
        return publish_result.error();
    }
    return record_result.value();
}

os::core::Result<void>
Supervisor::unregister_process(os::core::ProcessId process_id) noexcept {
    auto* entry = find_process_by_id(process_id);
    if (entry == nullptr) {
        return security_error(os::core::errors::security::unknown_process);
    }
    if (entry->managed_service) {
        return service_error(os::core::errors::service::invalid_request);
    }

    if (state_ == ServiceState::running && control_.valid()) {
        if (next_identity_request_id_ == 0U) {
            return os::core::make_error(
                os::core::ErrorDomain::ipc,
                os::ipc::errors::request_id_exhausted);
        }
        const auto request_id = os::core::RequestId{next_identity_request_id_};
        if (next_identity_request_id_ == std::numeric_limits<std::uint64_t>::max()) {
            next_identity_request_id_ = 0U;
        } else {
            ++next_identity_request_id_;
        }
        auto unregister_result = os::service::unregister_identity_with_service(
            control_, process_id, request_id, config_.descriptor.readiness_timeout_ms);
        if (!unregister_result &&
            !(unregister_result.error().domain == os::core::ErrorDomain::security &&
              unregister_result.error().code == os::core::errors::security::unknown_process)) {
            return unregister_result.error();
        }
    }
    remove_process_entry(process_id);
    return {};
}

os::core::Result<os::service::ProcessIdentityRecord>
Supervisor::lookup_process(pid_t native_pid) const noexcept {
    const auto* entry = find_process_by_pid(native_pid);
    if (entry == nullptr || !entry->occupied) {
        return security_error(os::core::errors::security::unknown_process);
    }
    auto authoritative = authority_->lookup(entry->record.peer.process);
    if (!authoritative) return authoritative.error();
    return authoritative.value();
}

os::core::Result<os::ipc::Channel> Supervisor::connect() noexcept {
    if (state_ != ServiceState::running || !client_endpoint_.valid()) {
        return service_error(os::core::errors::service::not_running);
    }

    int duplicate = -1;
    do {
        duplicate = ::fcntl(client_endpoint_.native_fd(), F_DUPFD_CLOEXEC, 0);
    } while (duplicate < 0 && errno == EINTR);
    if (duplicate < 0) {
        return service_error(os::core::errors::service::launch_failed);
    }

    return os::ipc::Channel::adopt(os::core::NativeHandle{duplicate});
}

ServiceStatus Supervisor::status() const noexcept {
    return ServiceStatus{
        .state = state_,
        .identity = service_identity_,
        .native_pid = child_pid_,
        .generation = generation_,
        .restarts_in_window = static_cast<std::uint32_t>(restart_history_count_),
    };
}

os::core::Result<void> Supervisor::terminate(int signal_number) noexcept {
    if (child_pid_ <= 0 || state_ != ServiceState::running) {
        return service_error(os::core::errors::service::not_running);
    }
    if (::kill(child_pid_, signal_number) != 0) {
        return service_error(os::core::errors::service::launch_failed);
    }
    return {};
}

void Supervisor::close_runtime_channels() noexcept {
    control_.close();
    client_endpoint_.close();
}

} // namespace os::supervisor
