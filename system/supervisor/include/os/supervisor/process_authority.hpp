#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <sys/types.h>

#include <os/core/identity.hpp>
#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/service/identity.hpp>

namespace os::supervisor {

// M2.9 boot-scoped identity allocator. Multiple Supervisor instances may share
// one ProcessAuthority so the same native execution receives one logical
// ProcessId across Storage, Keys and later platform services.
//
// The authority owns one pidfd per authoritative record. A service receives a
// duplicate only when its private identity registry is published. Linux PID is
// therefore process evidence, never the logical identity or authorization key.
inline constexpr std::size_t max_authoritative_processes = 128U;

class ProcessAuthority final {
public:
    ProcessAuthority() noexcept = default;

    ProcessAuthority(const ProcessAuthority&) = delete;
    ProcessAuthority& operator=(const ProcessAuthority&) = delete;

    // Acquire one publication reference for a live native process. Exact replay
    // of PID + PrincipalId + UserId returns the same boot-scoped ProcessId.
    // A different principal/user for the same live process is rejected.
    [[nodiscard]] os::core::Result<os::service::ProcessIdentityRecord>
    acquire(
        pid_t native_pid,
        os::core::PrincipalId principal,
        os::core::UserId user) noexcept;

    // Release one publication reference. The final release forgets the logical
    // ProcessId even if the native process remains alive; a later registration
    // is a fresh authorization epoch and receives a new ProcessId.
    [[nodiscard]] os::core::Result<void>
    release(os::core::ProcessId process_id) noexcept;

    [[nodiscard]] os::core::Result<os::service::ProcessIdentityRecord>
    lookup(pid_t native_pid) const noexcept;

    [[nodiscard]] os::core::Result<os::service::ProcessIdentityRecord>
    lookup(os::core::ProcessId process_id) const noexcept;

    // Trusted service-publication helper. The returned pidfd is CLOEXEC and
    // move-only; callers may transfer its duplicate with SCM_RIGHTS.
    [[nodiscard]] os::core::Result<os::core::NativeHandle>
    duplicate_pidfd(os::core::ProcessId process_id) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct Entry final {
        bool occupied {false};
        std::uint32_t references {0U};
        os::service::ProcessIdentityRecord record {};
        os::core::NativeHandle pidfd {};
    };

    std::array<Entry, max_authoritative_processes> entries_ {};
    std::uint64_t next_process_id_ {1U};

    [[nodiscard]] Entry* find_pid(pid_t native_pid) noexcept;
    [[nodiscard]] const Entry* find_pid(pid_t native_pid) const noexcept;
    [[nodiscard]] Entry* find_process(os::core::ProcessId process_id) noexcept;
    [[nodiscard]] const Entry* find_process(os::core::ProcessId process_id) const noexcept;

    [[nodiscard]] static bool pidfd_alive(int fd) noexcept;
};

} // namespace os::supervisor
