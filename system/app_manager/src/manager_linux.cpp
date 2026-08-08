#include <os/app/manager.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/app/bootstrap.hpp>
#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>

namespace os::app {
namespace {

inline constexpr int application_executable_fd = 4;

[[nodiscard]] constexpr os::core::Error manager_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

[[nodiscard]] int duplicate_cloexec(int fd) noexcept {
    int result = -1;
    do {
        result = ::fcntl(fd, F_DUPFD_CLOEXEC, 10);
    } while (result < 0 && errno == EINTR);
    return result;
}

void close_extra_descriptors() noexcept {
#if defined(SYS_close_range)
    if (::syscall(SYS_close_range, 5U, ~0U, 0U) == 0) return;
#endif
    long limit = ::sysconf(_SC_OPEN_MAX);
    if (limit < 0 || limit > 4096) limit = 4096;
    for (int fd = 5; fd < static_cast<int>(limit); ++fd) {
        (void)::close(fd);
    }
}

[[nodiscard]] os::core::Result<os::core::NativeHandle>
open_executable_beneath(int root_fd, std::string_view path) noexcept {
    struct stat root_info {};
    if (root_fd < 0 || ::fstat(root_fd, &root_info) != 0 || !S_ISDIR(root_info.st_mode) || path.empty()) {
        return manager_error(manager_errors::executable_rejected);
    }

    const int root_copy = duplicate_cloexec(root_fd);
    if (root_copy < 0) return manager_error(manager_errors::executable_rejected);
    os::core::NativeHandle current{root_copy};

    std::size_t start = 0U;
    while (start < path.size()) {
        const auto separator = path.find('/', start);
        const bool final_segment = separator == std::string_view::npos;
        const std::size_t end = final_segment ? path.size() : separator;
        const std::size_t length = end - start;
        if (length == 0U || length > os::package::max_manifest_path_segment_bytes) {
            return manager_error(manager_errors::executable_rejected);
        }

        std::array<char, os::package::max_manifest_path_segment_bytes + 1U> segment{};
        std::copy_n(path.data() + static_cast<std::ptrdiff_t>(start), length, segment.data());

        const int flags = final_segment
            ? (O_RDONLY | O_CLOEXEC | O_NOFOLLOW)
            : (O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        int next = -1;
        do {
            next = ::openat(current.native(), segment.data(), flags);
        } while (next < 0 && errno == EINTR);
        if (next < 0) return manager_error(manager_errors::executable_rejected);

        os::core::NativeHandle opened{next};
        if (final_segment) {
            struct stat executable_info {};
            if (::fstat(opened.native(), &executable_info) != 0 ||
                !S_ISREG(executable_info.st_mode) ||
                (executable_info.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0) {
                return manager_error(manager_errors::executable_rejected);
            }
            return opened;
        }

        current = std::move(opened);
        start = end + 1U;
    }

    return manager_error(manager_errors::executable_rejected);
}

[[nodiscard]] bool set_cloexec(int fd) noexcept {
    int flags = -1;
    do {
        flags = ::fcntl(fd, F_GETFD);
    } while (flags < 0 && errno == EINTR);
    if (flags < 0) return false;
    int result = -1;
    do {
        result = ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    } while (result < 0 && errno == EINTR);
    return result == 0;
}

[[noreturn]] void exec_application_child(
    int executable_fd,
    int bootstrap_fd,
    const os::sandbox::SandboxPolicyV1& sandbox) noexcept {
    const int bootstrap_temp = duplicate_cloexec(bootstrap_fd);
    const int executable_temp = duplicate_cloexec(executable_fd);
    if (bootstrap_temp < 0 || executable_temp < 0) std::_Exit(120);

    if (::dup2(bootstrap_temp, application_bootstrap_fd) < 0 ||
        ::dup2(executable_temp, application_executable_fd) < 0 ||
        !set_cloexec(application_executable_fd)) {
        std::_Exit(121);
    }

    (void)::close(bootstrap_temp);
    (void)::close(executable_temp);
    close_extra_descriptors();

    // M1.3 launches from an already-opened trusted executable descriptor.
    // Landlock admission for per-app package/data roots is intentionally M1.4.
    auto sandbox_result = os::sandbox::apply_before_exec("/proc/self/fd/4", sandbox);
    if (!sandbox_result) std::_Exit(122);

    char arg0[] = "emnl-app";
    char path_env[] = "PATH=/usr/bin:/bin";
    char lang_env[] = "LANG=C";
    char* const arguments[] = {arg0, nullptr};
    char* const environment[] = {path_env, lang_env, nullptr};

#if defined(SYS_execveat)
    (void)::syscall(
        SYS_execveat,
        application_executable_fd,
        "",
        arguments,
        environment,
        AT_EMPTY_PATH);
#else
    (void)::fexecve(application_executable_fd, arguments, environment);
#endif
    std::_Exit(127);
}

void kill_and_reap(pid_t pid) noexcept {
    if (pid <= 0) return;
    (void)::kill(pid, SIGKILL);
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
}

[[nodiscard]] bool is_unknown_process(const os::core::Error& error) noexcept {
    return error.domain == os::core::ErrorDomain::security &&
        error.code == os::core::errors::security::unknown_process;
}

} // namespace

ApplicationManager::ApplicationManager(
    os::package::PersistentPackageRegistry& packages,
    os::supervisor::Supervisor& supervisor) noexcept
    : packages_(packages), supervisor_(supervisor) {}

ApplicationManager::~ApplicationManager() {
    for (auto& slot : instances_) {
        if (!slot.occupied) continue;
        kill_and_reap(slot.info.native_pid);
        os::core::discard_result(supervisor_.unregister_process(slot.info.identity.process));
        slot = InstanceSlot{};
    }
}

ApplicationManager::LaunchTarget*
ApplicationManager::find_target(const os::package::PackageGenerationRecord& package) noexcept {
    for (auto& target : targets_) {
        if (target.occupied && target.package == package) return &target;
    }
    return nullptr;
}

const ApplicationManager::LaunchTarget*
ApplicationManager::find_target(const os::package::PackageGenerationRecord& package) const noexcept {
    for (const auto& target : targets_) {
        if (target.occupied && target.package == package) return &target;
    }
    return nullptr;
}

ApplicationManager::InstanceSlot*
ApplicationManager::find_instance(os::core::ApplicationInstanceId instance_id) noexcept {
    for (auto& slot : instances_) {
        if (slot.occupied && slot.info.instance == instance_id) return &slot;
    }
    return nullptr;
}

const ApplicationManager::InstanceSlot*
ApplicationManager::find_instance(os::core::ApplicationInstanceId instance_id) const noexcept {
    for (const auto& slot : instances_) {
        if (slot.occupied && slot.info.instance == instance_id) return &slot;
    }
    return nullptr;
}

os::core::Result<void>
ApplicationManager::register_launch_target(LaunchTargetRegistration registration) noexcept {
    if (!registration.package.valid() || !os::core::valid_principal(registration.principal) ||
        !registration.generation_directory.valid() || !registration.entry_point.valid() ||
        registration.readiness_timeout_ms == 0U || registration.sandbox.max_open_files < 5U ||
        registration.sandbox.max_processes == 0U) {
        return manager_error(manager_errors::invalid_target);
    }
    if (registration.sandbox.require_landlock) {
        return os::core::make_error(
            os::core::ErrorDomain::security,
            os::core::errors::security::sandbox_not_supported);
    }

    auto known = packages_.generation(
        registration.package.application,
        registration.package.generation);
    if (!known || known.value() != registration.package) {
        return manager_error(manager_errors::invalid_target);
    }
    if (find_target(registration.package) != nullptr) {
        return manager_error(manager_errors::target_conflict);
    }

    for (const auto& target : targets_) {
        if (!target.occupied || target.package.application != registration.package.application) continue;
        if (target.principal != registration.principal) {
            return manager_error(manager_errors::principal_mismatch);
        }
    }

    LaunchTarget* free_target = nullptr;
    for (auto& target : targets_) {
        if (!target.occupied) {
            free_target = &target;
            break;
        }
    }
    if (free_target == nullptr) return manager_error(manager_errors::target_capacity);

    auto executable = open_executable_beneath(
        registration.generation_directory.native(),
        registration.entry_point.view());
    if (!executable) return executable.error();

    free_target->occupied = true;
    free_target->package = registration.package;
    free_target->principal = registration.principal;
    free_target->executable = std::move(executable).value();
    free_target->sandbox = registration.sandbox;
    free_target->readiness_timeout_ms = registration.readiness_timeout_ms;
    return {};
}

os::core::Result<ApplicationInstanceInfo>
ApplicationManager::launch(const os::package::PackageId& package_id, os::core::UserId user) noexcept {
    auto owner = packages_.owner(package_id);
    if (!owner) return owner.error();
    auto active = packages_.active(owner.value());
    if (!active) return active.error();

    LaunchTarget* target = find_target(active.value());
    if (target == nullptr || !target->executable.valid()) {
        return manager_error(manager_errors::target_not_found);
    }

    InstanceSlot* free_instance = nullptr;
    for (auto& slot : instances_) {
        if (!slot.occupied) {
            free_instance = &slot;
            break;
        }
    }
    if (free_instance == nullptr) return manager_error(manager_errors::instance_capacity);
    if (next_instance_id_ == 0U) return manager_error(manager_errors::instance_id_exhausted);

    const auto instance_id = os::core::ApplicationInstanceId{next_instance_id_};
    if (next_instance_id_ == std::numeric_limits<std::uint64_t>::max()) {
        next_instance_id_ = 0U;
    } else {
        ++next_instance_id_;
    }

    auto bootstrap_pair_result = os::ipc::Channel::create_local_pair();
    if (!bootstrap_pair_result) return bootstrap_pair_result.error();
    auto bootstrap_pair = std::move(bootstrap_pair_result).value();

    const pid_t child = ::fork();
    if (child < 0) return manager_error(os::core::errors::service::launch_failed);
    if (child == 0) {
        bootstrap_pair[0].close();
        exec_application_child(
            target->executable.native(),
            bootstrap_pair[1].native_fd(),
            target->sandbox);
    }

    bootstrap_pair[1].close();

    auto identity = supervisor_.register_process(child, target->principal, user);
    if (!identity) {
        kill_and_reap(child);
        return identity.error();
    }

    const ApplicationBootstrapRecordV1 bootstrap{
        .instance = instance_id,
        .identity = identity.value().peer,
        .package_generation = target->package.generation.value(),
    };
    auto send = send_bootstrap_request(bootstrap_pair[0], bootstrap);
    if (!send) {
        os::core::discard_result(supervisor_.unregister_process(identity.value().peer.process));
        kill_and_reap(child);
        return send.error();
    }

    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer{};
    auto ready = wait_for_ready(
        bootstrap_pair[0],
        receive_buffer,
        bootstrap,
        target->readiness_timeout_ms);
    if (!ready) {
        os::core::discard_result(supervisor_.unregister_process(identity.value().peer.process));
        kill_and_reap(child);
        return ready.error();
    }

    const ApplicationInstanceInfo info{
        .instance = instance_id,
        .application = target->package.application,
        .generation = target->package.generation,
        .content = target->package.content,
        .identity = identity.value().peer,
        .native_pid = child,
    };
    if (!info.valid()) {
        os::core::discard_result(supervisor_.unregister_process(identity.value().peer.process));
        kill_and_reap(child);
        return manager_error(manager_errors::invalid_target);
    }

    free_instance->occupied = true;
    free_instance->info = info;
    return info;
}

os::core::Result<void> ApplicationManager::maintain() noexcept {
    for (auto& slot : instances_) {
        if (!slot.occupied) continue;
        int status = 0;
        pid_t result = -1;
        do {
            result = ::waitpid(slot.info.native_pid, &status, WNOHANG);
        } while (result < 0 && errno == EINTR);

        if (result == 0) continue;
        if (result < 0) return manager_error(os::core::errors::service::launch_failed);
        if (result == slot.info.native_pid) {
            auto unregister = supervisor_.unregister_process(slot.info.identity.process);
            if (!unregister && !is_unknown_process(unregister.error())) return unregister.error();
            slot = InstanceSlot{};
        }
    }
    return {};
}

os::core::Result<ApplicationInstanceInfo>
ApplicationManager::instance(os::core::ApplicationInstanceId instance_id) const noexcept {
    if (instance_id.value() == 0U) return manager_error(manager_errors::unknown_instance);
    const auto* slot = find_instance(instance_id);
    if (slot == nullptr) return manager_error(manager_errors::unknown_instance);
    return slot->info;
}

os::core::Result<void>
ApplicationManager::terminate(
    os::core::ApplicationInstanceId instance_id,
    int signal_number) noexcept {
    auto* slot = find_instance(instance_id);
    if (slot == nullptr) return manager_error(manager_errors::unknown_instance);
    if (signal_number <= 0 || ::kill(slot->info.native_pid, signal_number) != 0) {
        return manager_error(os::core::errors::service::launch_failed);
    }
    return {};
}

} // namespace os::app
