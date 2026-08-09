#include <os/supervisor/process_authority.hpp>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <os/core/error.hpp>

namespace os::supervisor {
namespace {

[[nodiscard]] constexpr os::core::Error service_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

[[nodiscard]] constexpr os::core::Error security_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::security, code);
}

[[nodiscard]] os::core::Result<os::core::NativeHandle> open_pidfd(pid_t pid) noexcept {
#if defined(SYS_pidfd_open)
    int fd = -1;
    do {
        fd = static_cast<int>(::syscall(SYS_pidfd_open, pid, 0U));
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) return service_error(os::core::errors::service::not_supported);

    int flags = -1;
    do {
        flags = ::fcntl(fd, F_GETFD);
    } while (flags < 0 && errno == EINTR);
    if (flags < 0 || ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0) {
        (void)::close(fd);
        return service_error(os::core::errors::service::launch_failed);
    }
    return os::core::NativeHandle{fd};
#else
    (void)pid;
    return service_error(os::core::errors::service::not_supported);
#endif
}

[[nodiscard]] os::core::Result<os::ipc::KernelPeerCredentials>
credentials_for_pid(pid_t pid) noexcept {
    if (pid <= 0) return security_error(os::core::errors::security::invalid_identity);

    char path[64]{};
    const int length = std::snprintf(path, sizeof(path), "/proc/%ld", static_cast<long>(pid));
    if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(path)) {
        return security_error(os::core::errors::security::invalid_identity);
    }

    struct stat info {};
    if (::stat(path, &info) != 0) {
        return security_error(os::core::errors::security::stale_process);
    }
    return os::ipc::KernelPeerCredentials{
        .process_id = static_cast<std::int64_t>(pid),
        .user_id = static_cast<std::uint32_t>(info.st_uid),
        .group_id = static_cast<std::uint32_t>(info.st_gid),
    };
}

} // namespace

bool ProcessAuthority::pidfd_alive(int fd) noexcept {
    if (fd < 0) return false;
    pollfd descriptor{.fd = fd, .events = POLLIN, .revents = 0};
    int result = -1;
    do {
        result = ::poll(&descriptor, 1, 0);
    } while (result < 0 && errno == EINTR);
    return result == 0;
}

ProcessAuthority::Entry* ProcessAuthority::find_pid(pid_t native_pid) noexcept {
    for (auto& entry : entries_) {
        if (entry.occupied &&
            entry.record.kernel.process_id == static_cast<std::int64_t>(native_pid)) {
            return &entry;
        }
    }
    return nullptr;
}

const ProcessAuthority::Entry* ProcessAuthority::find_pid(pid_t native_pid) const noexcept {
    for (const auto& entry : entries_) {
        if (entry.occupied &&
            entry.record.kernel.process_id == static_cast<std::int64_t>(native_pid)) {
            return &entry;
        }
    }
    return nullptr;
}

ProcessAuthority::Entry*
ProcessAuthority::find_process(os::core::ProcessId process_id) noexcept {
    for (auto& entry : entries_) {
        if (entry.occupied && entry.record.peer.process == process_id) return &entry;
    }
    return nullptr;
}

const ProcessAuthority::Entry*
ProcessAuthority::find_process(os::core::ProcessId process_id) const noexcept {
    for (const auto& entry : entries_) {
        if (entry.occupied && entry.record.peer.process == process_id) return &entry;
    }
    return nullptr;
}

os::core::Result<os::service::ProcessIdentityRecord>
ProcessAuthority::acquire(
    pid_t native_pid,
    os::core::PrincipalId principal,
    os::core::UserId user) noexcept {
    if (native_pid <= 0 || !os::core::valid_principal(principal) || next_process_id_ == 0U) {
        return security_error(os::core::errors::security::invalid_identity);
    }

    if (auto* existing = find_pid(native_pid); existing != nullptr) {
        if (!pidfd_alive(existing->pidfd.native())) {
            // A dead pidfd is exact generation evidence. Forget the stale
            // record even if a lagging service still holds a publication
            // reference; its transferred pidfd is dead and cannot authorize a
            // later process that reuses the numeric Linux PID.
            existing->pidfd.reset();
            *existing = Entry{};
        } else {
            if (existing->record.peer.principal != principal ||
                existing->record.peer.user != user) {
                return security_error(os::core::errors::security::credential_mismatch);
            }
            if (existing->references == std::numeric_limits<std::uint32_t>::max()) {
                return security_error(os::core::errors::security::registry_full);
            }
            ++existing->references;
            return existing->record;
        }
    }

    Entry* free_entry = nullptr;
    for (auto& entry : entries_) {
        if (!entry.occupied) {
            free_entry = &entry;
            break;
        }
    }
    if (free_entry == nullptr) {
        return security_error(os::core::errors::security::registry_full);
    }

    auto pidfd_result = open_pidfd(native_pid);
    if (!pidfd_result) return pidfd_result.error();
    auto pidfd = std::move(pidfd_result).value();

    auto credentials = credentials_for_pid(native_pid);
    if (!credentials) return credentials.error();
    if (!pidfd_alive(pidfd.native())) {
        return security_error(os::core::errors::security::stale_process);
    }

    const os::core::ProcessId process{next_process_id_};
    if (next_process_id_ == std::numeric_limits<std::uint64_t>::max()) {
        next_process_id_ = 0U;
    } else {
        ++next_process_id_;
    }

    const os::service::ProcessIdentityRecord record{
        .kernel = credentials.value(),
        .peer = os::core::PeerIdentity{
            .principal = principal,
            .user = user,
            .process = process,
        },
    };
    *free_entry = Entry{
        .occupied = true,
        .references = 1U,
        .record = record,
        .pidfd = std::move(pidfd),
    };
    return record;
}

os::core::Result<void>
ProcessAuthority::release(os::core::ProcessId process_id) noexcept {
    if (process_id.value() == 0U) {
        return security_error(os::core::errors::security::invalid_identity);
    }
    auto* entry = find_process(process_id);
    if (entry == nullptr) {
        return security_error(os::core::errors::security::unknown_process);
    }
    if (entry->references > 1U) {
        --entry->references;
        return {};
    }
    entry->pidfd.reset();
    *entry = Entry{};
    return {};
}

os::core::Result<os::service::ProcessIdentityRecord>
ProcessAuthority::lookup(pid_t native_pid) const noexcept {
    const auto* entry = find_pid(native_pid);
    if (entry == nullptr) {
        return security_error(os::core::errors::security::unknown_process);
    }
    if (!pidfd_alive(entry->pidfd.native())) {
        return security_error(os::core::errors::security::stale_process);
    }
    return entry->record;
}

os::core::Result<os::service::ProcessIdentityRecord>
ProcessAuthority::lookup(os::core::ProcessId process_id) const noexcept {
    const auto* entry = find_process(process_id);
    if (entry == nullptr) {
        return security_error(os::core::errors::security::unknown_process);
    }
    if (!pidfd_alive(entry->pidfd.native())) {
        return security_error(os::core::errors::security::stale_process);
    }
    return entry->record;
}

os::core::Result<os::core::NativeHandle>
ProcessAuthority::duplicate_pidfd(os::core::ProcessId process_id) const noexcept {
    const auto* entry = find_process(process_id);
    if (entry == nullptr) {
        return security_error(os::core::errors::security::unknown_process);
    }
    if (!pidfd_alive(entry->pidfd.native())) {
        return security_error(os::core::errors::security::stale_process);
    }

    int duplicate = -1;
    do {
        duplicate = ::fcntl(entry->pidfd.native(), F_DUPFD_CLOEXEC, 0);
    } while (duplicate < 0 && errno == EINTR);
    if (duplicate < 0) return service_error(os::core::errors::service::launch_failed);
    return os::core::NativeHandle{duplicate};
}

std::size_t ProcessAuthority::size() const noexcept {
    std::size_t count = 0U;
    for (const auto& entry : entries_) {
        if (entry.occupied) ++count;
    }
    return count;
}

} // namespace os::supervisor
