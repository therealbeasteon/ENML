#include <os/app/manager.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
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
#include <os/keys/service.hpp>
#include <os/storage/service.hpp>

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

void close_extra_descriptors(unsigned first_fd) noexcept {
#if defined(SYS_close_range)
    if (::syscall(SYS_close_range, first_fd, ~0U, 0U) == 0) return;
#endif
    long limit = ::sysconf(_SC_OPEN_MAX);
    if (limit < 0 || limit > 4096) limit = 4096;
    for (int fd = static_cast<int>(first_fd); fd < static_cast<int>(limit); ++fd) {
        (void)::close(fd);
    }
}

[[nodiscard]] os::core::Result<os::core::NativeHandle>
open_executable_beneath(int root_fd, std::string_view path) noexcept {
    struct stat root_info {};
    if (root_fd < 0 || ::fstat(root_fd, &root_info) != 0 || !S_ISDIR(root_info.st_mode) || path.empty()) {
        return manager_error(manager_errors::executable_rejected);
    }

    int root_path_fd = -1;
    do {
        root_path_fd = ::openat(root_fd, ".", O_PATH | O_DIRECTORY | O_CLOEXEC);
    } while (root_path_fd < 0 && errno == EINTR);
    if (root_path_fd < 0) return manager_error(manager_errors::executable_rejected);
    os::core::NativeHandle current{root_path_fd};

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
            ? (O_PATH | O_CLOEXEC | O_NOFOLLOW)
            : (O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
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

[[nodiscard]] os::core::Result<os::core::NativeHandle>
open_private_data_root(int directory_fd) noexcept {
    struct stat metadata {};
    if (directory_fd < 0 || ::fstat(directory_fd, &metadata) != 0 || !S_ISDIR(metadata.st_mode)) {
        return manager_error(manager_errors::invalid_profile);
    }
    int root = -1;
    do {
        root = ::openat(directory_fd, ".", O_PATH | O_DIRECTORY | O_CLOEXEC);
    } while (root < 0 && errno == EINTR);
    if (root < 0) return manager_error(manager_errors::invalid_profile);
    return os::core::NativeHandle{root};
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
    int legacy_storage_service_fd,
    int bootstrap_fd,
    const os::sandbox::SandboxPolicyV1& sandbox) noexcept {
    const int bootstrap_temp = duplicate_cloexec(bootstrap_fd);
    const int executable_temp = duplicate_cloexec(executable_fd);
    const int storage_temp = legacy_storage_service_fd >= 0
        ? duplicate_cloexec(legacy_storage_service_fd)
        : -1;
    if (bootstrap_temp < 0 || executable_temp < 0 ||
        (legacy_storage_service_fd >= 0 && storage_temp < 0)) {
        std::_Exit(120);
    }

    if (::dup2(bootstrap_temp, application_bootstrap_fd) < 0 ||
        ::dup2(executable_temp, application_executable_fd) < 0 ||
        !set_cloexec(application_executable_fd)) {
        std::_Exit(121);
    }
    if (legacy_storage_service_fd >= 0 &&
        ::dup2(storage_temp, application_storage_service_fd) < 0) {
        std::_Exit(121);
    }

    (void)::close(bootstrap_temp);
    (void)::close(executable_temp);
    if (storage_temp >= 0) (void)::close(storage_temp);

    if (legacy_storage_service_fd >= 0) {
        close_extra_descriptors(
            static_cast<unsigned>(application_storage_service_fd + 1));
    } else {
        // Bootstrap v2 has no preassigned service fd. Close fd 5 and every
        // higher inherited descriptor before sandbox/exec. Service endpoints
        // arrive later only as typed SCM_RIGHTS capabilities on fd 3.
        close_extra_descriptors(static_cast<unsigned>(application_storage_service_fd));
    }

    const os::sandbox::ApplicationSandboxHandlesV1 sandbox_handles{
        .executable_fd = application_executable_fd,
        .private_data_directory_fd = -1,
    };
    auto sandbox_result = os::sandbox::apply_application_before_exec(sandbox_handles, sandbox);
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

[[nodiscard]] bool is_already_detached(const os::core::Error& error) noexcept {
    return is_unknown_process(error) ||
        (error.domain == os::core::ErrorDomain::service &&
         error.code == os::supervisor::broker_errors::process_not_attached);
}

} // namespace

ApplicationManager::ApplicationManager(
    os::package::PersistentPackageRegistry& packages,
    ApplicationPrincipalStore& principals,
    os::supervisor::Supervisor& supervisor) noexcept
    : packages_(packages), principals_(principals), supervisor_(supervisor) {}

ApplicationManager::~ApplicationManager() {
    for (auto& slot : instances_) {
        if (!slot.occupied) continue;
        // Revoke service identity before killing the process. The process may
        // still possess endpoint fds, but services no longer resolve its sender
        // credentials to authority once this call completes.
        os::core::discard_result(release_instance_identity(slot.info.identity.process));
        kill_and_reap(slot.info.native_pid);
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

ApplicationManager::ApplicationProfile*
ApplicationManager::find_profile(
    const os::package::ApplicationIdentity& application,
    os::core::UserId user) noexcept {
    for (auto& profile : profiles_) {
        if (profile.occupied && profile.application == application && profile.user == user) return &profile;
    }
    return nullptr;
}

const ApplicationManager::ApplicationProfile*
ApplicationManager::find_profile(
    const os::package::ApplicationIdentity& application,
    os::core::UserId user) const noexcept {
    for (const auto& profile : profiles_) {
        if (profile.occupied && profile.application == application && profile.user == user) return &profile;
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
    if (!registration.package.valid() || !registration.generation_directory.valid() ||
        !registration.entry_point.valid() || registration.readiness_timeout_ms == 0U) {
        return manager_error(manager_errors::invalid_target);
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
    free_target->executable = std::move(executable).value();
    free_target->readiness_timeout_ms = registration.readiness_timeout_ms;
    return {};
}

os::core::Result<void>
ApplicationManager::register_application_profile(ApplicationProfileRegistration registration) noexcept {
    if (!registration.application.valid() || !registration.private_data_directory.valid() ||
        registration.sandbox.max_open_files < 6U || registration.sandbox.max_processes == 0U) {
        return manager_error(manager_errors::invalid_profile);
    }
    auto owner = packages_.owner(registration.application.package_id);
    if (!owner || owner.value() != registration.application) {
        return manager_error(manager_errors::invalid_profile);
    }
    if (find_profile(registration.application, registration.user) != nullptr) {
        return manager_error(manager_errors::profile_conflict);
    }

    auto current_profiles = republish_profiles_if_needed();
    if (!current_profiles) return current_profiles.error();

    ApplicationProfile* free_profile = nullptr;
    for (auto& profile : profiles_) {
        if (!profile.occupied) {
            free_profile = &profile;
            break;
        }
    }
    if (free_profile == nullptr) return manager_error(manager_errors::profile_capacity);

    auto principal = principals_.resolve_or_allocate(registration.application, registration.user);
    if (!principal) return principal.error();
    auto data_root = open_private_data_root(registration.private_data_directory.native());
    if (!data_root) return data_root.error();

    free_profile->occupied = true;
    free_profile->application = registration.application;
    free_profile->user = registration.user;
    free_profile->principal = principal.value().principal;
    free_profile->private_data_directory = std::move(data_root).value();
    free_profile->sandbox = registration.sandbox;

    auto published = publish_profile(*free_profile);
    if (!published) {
        *free_profile = ApplicationProfile{};
        return published.error();
    }
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
    ApplicationProfile* profile = find_profile(owner.value(), user);
    if (profile == nullptr || !profile->private_data_directory.valid() ||
        !os::core::valid_principal(profile->principal)) {
        return manager_error(manager_errors::profile_not_found);
    }

    const bool broker_mode = service_broker_ != nullptr;
    if (broker_mode && !broker_configuration_valid()) {
        return manager_error(manager_errors::broker_misconfigured);
    }

    auto storage_policy = republish_profiles_if_needed();
    if (!storage_policy) return storage_policy.error();
    // An uninstall disables and revokes this retained profile without deleting
    // its private data. A later same-signer reinstall becomes launch-eligible
    // again only after this explicit fresh publication; old bearer endpoints
    // remain dead and are never resurrected.
    auto current_profile_policy = publish_profile(*profile);
    if (!current_profile_policy) return current_profile_policy.error();

    os::ipc::Channel legacy_storage_connection{};
    if (!broker_mode) {
        auto storage_connection_result = supervisor_.connect();
        if (!storage_connection_result) return storage_connection_result.error();
        legacy_storage_connection = std::move(storage_connection_result).value();
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
            broker_mode ? -1 : legacy_storage_connection.native_fd(),
            bootstrap_pair[1].native_fd(),
            profile->sandbox);
    }

    bootstrap_pair[1].close();
    legacy_storage_connection.close();

    os::service::ProcessIdentityRecord identity_record{};
    if (broker_mode) {
        const std::array services{
            os::storage::storage_service_id,
            os::keys::key_service_id,
        };
        auto attached = service_broker_->attach_process(
            child,
            profile->principal,
            user,
            std::span<const os::core::ServiceId>{services});
        if (!attached) {
            kill_and_reap(child);
            return attached.error();
        }
        identity_record = attached.value();
    } else {
        auto identity = supervisor_.register_process(child, profile->principal, user);
        if (!identity) {
            kill_and_reap(child);
            return identity.error();
        }
        identity_record = identity.value();
    }

    const ApplicationBootstrapRecordV1 bootstrap{
        .instance = instance_id,
        .identity = identity_record.peer,
        .package_generation = target->package.generation.value(),
    };

    os::core::Result<void> send{};
    if (broker_mode) {
        auto storage_channel_result = service_broker_->connect(
            identity_record.peer.process,
            os::storage::storage_service_id);
        if (!storage_channel_result) {
            auto released = release_instance_identity(identity_record.peer.process);
            kill_and_reap(child);
            if (!released && !is_already_detached(released.error())) return released.error();
            return storage_channel_result.error();
        }
        auto key_channel_result = service_broker_->connect(
            identity_record.peer.process,
            os::keys::key_service_id);
        if (!key_channel_result) {
            auto released = release_instance_identity(identity_record.peer.process);
            kill_and_reap(child);
            if (!released && !is_already_detached(released.error())) return released.error();
            return key_channel_result.error();
        }

        auto storage_channel = std::move(storage_channel_result).value();
        auto key_channel = std::move(key_channel_result).value();
        std::array<os::core::NativeHandle, 2U> endpoints{
            storage_channel.take_native_handle_for_transfer(),
            key_channel.take_native_handle_for_transfer(),
        };
        const std::array services{
            os::storage::storage_service_id,
            os::keys::key_service_id,
        };
        send = send_bootstrap_request_v2(
            bootstrap_pair[0],
            bootstrap,
            std::span<const os::core::ServiceId>{services},
            std::span<const os::core::NativeHandle>{endpoints});
    } else {
        send = send_bootstrap_request(bootstrap_pair[0], bootstrap);
    }

    if (!send) {
        auto released = release_instance_identity(identity_record.peer.process);
        kill_and_reap(child);
        if (!released && !is_already_detached(released.error())) return released.error();
        return send.error();
    }

    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer{};
    os::core::Result<void> ready{};
    if (broker_mode) {
        const std::array services{
            os::storage::storage_service_id,
            os::keys::key_service_id,
        };
        ready = wait_for_ready_v2(
            bootstrap_pair[0],
            receive_buffer,
            bootstrap,
            std::span<const os::core::ServiceId>{services},
            target->readiness_timeout_ms);
    } else {
        ready = wait_for_ready(
            bootstrap_pair[0],
            receive_buffer,
            bootstrap,
            target->readiness_timeout_ms);
    }
    if (!ready) {
        auto released = release_instance_identity(identity_record.peer.process);
        kill_and_reap(child);
        if (!released && !is_already_detached(released.error())) return released.error();
        return ready.error();
    }

    const ApplicationInstanceInfo info{
        .instance = instance_id,
        .application = target->package.application,
        .generation = target->package.generation,
        .content = target->package.content,
        .identity = identity_record.peer,
        .native_pid = child,
    };
    if (!info.valid()) {
        auto released = release_instance_identity(identity_record.peer.process);
        kill_and_reap(child);
        if (!released && !is_already_detached(released.error())) return released.error();
        return manager_error(manager_errors::invalid_target);
    }

    free_instance->occupied = true;
    free_instance->info = info;
    if (broker_mode) {
        // Bootstrap-v2 READY transitions fd 3 from one-shot initialization into
        // the M2.10 long-lived platform-service session. Retain only the parent
        // endpoint and the trusted service set that was actually bootstrapped.
        free_instance->service_session = std::move(bootstrap_pair[0]);
        free_instance->services[0] = os::storage::storage_service_id;
        free_instance->services[1] = os::keys::key_service_id;
        free_instance->service_count = 2U;
    }
    return info;
}

os::core::Result<void> ApplicationManager::maintain() noexcept {
    auto storage_service = supervisor_.maintain();
    if (!storage_service && supervisor_.status().state == os::supervisor::ServiceState::crash_loop) {
        return storage_service.error();
    }

    if (key_supervisor_ != nullptr) {
        auto key_service = key_supervisor_->maintain();
        if (!key_service && key_supervisor_->status().state == os::supervisor::ServiceState::crash_loop) {
            return key_service.error();
        }
    }

    if (supervisor_.status().state == os::supervisor::ServiceState::running &&
        (key_supervisor_ == nullptr ||
         key_supervisor_->status().state == os::supervisor::ServiceState::running)) {
        auto roots = republish_profiles_if_needed();
        if (!roots) return roots.error();
    }

    for (auto& slot : instances_) {
        if (!slot.occupied) continue;

        // Service lifecycle/policy reconciliation above happens first. A
        // blocked application reacquisition therefore sees only the current
        // ready generation and freshly-republished policy, never an endpoint to
        // a half-initialized replacement service.
        auto session = service_runtime_session_if_pending(slot);
        if (!session) return session.error();

        int status = 0;
        pid_t result = -1;
        do {
            result = ::waitpid(slot.info.native_pid, &status, WNOHANG);
        } while (result < 0 && errno == EINTR);

        if (result == 0) continue;
        if (result < 0) return manager_error(os::core::errors::service::launch_failed);
        if (result == slot.info.native_pid) {
            auto released = release_instance_identity(slot.info.identity.process);
            if (!released && !is_already_detached(released.error())) return released.error();
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