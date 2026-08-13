#include <os/sandbox/substrate.hpp>

#include <cerrno>

#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <os/core/error.hpp>

namespace os::sandbox {
namespace {

[[nodiscard]] constexpr os::core::Error substrate_error(std::uint32_t code) noexcept {
    // Whether a service can be confined is a security answer, not a
    // configuration detail.
    return os::core::make_error(os::core::ErrorDomain::security, code);
}

// PR_SET_NO_NEW_PRIVS cannot be probed by setting it - the bit is one-way for
// the process and would silently change this process's behaviour. Reading it
// distinguishes a kernel that knows the option from one that does not.
[[nodiscard]] bool probe_no_new_privs() noexcept {
    errno = 0;
    const int result = ::prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0);
    return result >= 0;
}

// PR_GET_SECCOMP answers whether the kernel has seccomp at all without
// installing anything. A kernel without it fails with EINVAL.
[[nodiscard]] bool probe_seccomp() noexcept {
    errno = 0;
    const int result = ::prctl(PR_GET_SECCOMP, 0, 0, 0, 0);
    return result >= 0;
}

// Landlock is probed by asking the kernel which ABI version it implements,
// which allocates nothing and enforces nothing. A kernel without Landlock
// fails with ENOSYS; one built without it fails with EOPNOTSUPP.
[[nodiscard]] bool probe_landlock() noexcept {
#if defined(__NR_landlock_create_ruleset)
    errno = 0;
    const long version = ::syscall(
        __NR_landlock_create_ruleset,
        nullptr,
        static_cast<std::size_t>(0),
        static_cast<std::uint32_t>(1U /* LANDLOCK_CREATE_RULESET_VERSION */));
    return version > 0;
#else
    return false;
#endif
}

[[nodiscard]] bool probe_resource_limits() noexcept {
    struct rlimit limit {};
    return ::getrlimit(RLIMIT_NOFILE, &limit) == 0;
}

// The one capability whose absence is not a degradation. ENML's typed
// identities are meaningful only because the peer credentials come from the
// kernel; a substrate without this is a different operating system, not a
// weaker configuration of this one.
[[nodiscard]] bool probe_attested_ipc() noexcept {
    const int socket_fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (socket_fd < 0) {
        return false;
    }
    int enable = 1;
    const bool credentials_available =
        ::setsockopt(socket_fd, SOL_SOCKET, SO_PASSCRED, &enable, sizeof(enable)) == 0;
    ::close(socket_fd);
    return credentials_available;
}

} // namespace

os::core::Result<SubstrateReport> SubstrateReport::create(
    std::uint32_t capabilities) noexcept {
    if ((capabilities & ~known_substrate_capabilities) != 0U) {
        return os::core::Result<SubstrateReport>{
            substrate_error(substrate_errors::unknown_capability)};
    }
    SubstrateReport report{};
    report.capabilities_ = capabilities;
    return os::core::Result<SubstrateReport>{report};
}

os::core::Result<void> SubstrateReport::satisfies(
    const SandboxPolicyV1& policy) const noexcept {
    // A disabled policy asks for nothing, so any substrate satisfies it. The
    // service is not claiming confinement, so there is nothing to be unable to
    // provide.
    if (!policy.enabled) {
        return {};
    }

    // Checked in the order a bypass would matter. Without no_new_privs an
    // execve can regain privilege, which makes the rest defeatable, so its
    // absence is reported first even when more is missing.
    if (policy.require_no_new_privs && !provides(SubstrateCapability::no_new_privs)) {
        return substrate_error(substrate_errors::no_new_privs_unavailable);
    }
    if (policy.require_seccomp && !provides(SubstrateCapability::seccomp_filter)) {
        return substrate_error(substrate_errors::seccomp_unavailable);
    }
    if (policy.require_landlock && !provides(SubstrateCapability::landlock)) {
        return substrate_error(substrate_errors::landlock_unavailable);
    }

    // The limit fields are unconditional rather than opt-in, so an enabled
    // policy always depends on them.
    if (!provides(SubstrateCapability::resource_limits)) {
        return substrate_error(substrate_errors::resource_limits_unavailable);
    }
    return {};
}

os::core::Result<SubstrateReport> probe_substrate() noexcept {
    std::uint32_t capabilities = 0U;
    if (probe_no_new_privs()) {
        capabilities |= static_cast<std::uint32_t>(SubstrateCapability::no_new_privs);
    }
    if (probe_seccomp()) {
        capabilities |= static_cast<std::uint32_t>(SubstrateCapability::seccomp_filter);
    }
    if (probe_landlock()) {
        capabilities |= static_cast<std::uint32_t>(SubstrateCapability::landlock);
    }
    if (probe_resource_limits()) {
        capabilities |= static_cast<std::uint32_t>(SubstrateCapability::resource_limits);
    }
    if (probe_attested_ipc()) {
        capabilities |= static_cast<std::uint32_t>(SubstrateCapability::attested_datagram_ipc);
    }
    return SubstrateReport::create(capabilities);
}

} // namespace os::sandbox
