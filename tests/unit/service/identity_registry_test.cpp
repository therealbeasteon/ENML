#include <cassert>
#include <cstdint>

#include <signal.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/core/native_handle.hpp>
#include <os/service/identity.hpp>

namespace {

os::core::NativeHandle pidfd_for(pid_t pid) {
#if defined(SYS_pidfd_open)
    const int fd = static_cast<int>(::syscall(SYS_pidfd_open, pid, 0U));
    assert(fd >= 0);
    return os::core::NativeHandle{fd};
#else
    (void)pid;
    assert(false && "pidfd_open required for M0.7");
    return {};
#endif
}

os::service::ProcessIdentityRecord record_for(
    pid_t pid,
    os::core::ProcessId process,
    os::core::PrincipalId principal,
    os::core::UserId user) {
    return os::service::ProcessIdentityRecord{
        .kernel = os::ipc::KernelPeerCredentials{
            .process_id = static_cast<std::int64_t>(pid),
            .user_id = static_cast<std::uint32_t>(::getuid()),
            .group_id = static_cast<std::uint32_t>(::getgid()),
        },
        .peer = os::core::PeerIdentity{
            .principal = principal,
            .user = user,
            .process = process,
        },
    };
}

} // namespace

int main() {
    os::service::IdentityRegistry registry;
    const auto self_record = record_for(
        ::getpid(), os::core::ProcessId{9U}, os::core::PrincipalId{0x10U, 0x20U}, os::core::UserId{4U});
    assert(registry.register_process(self_record, pidfd_for(::getpid())));
    assert(registry.size() == 1U);

    auto resolved = registry.resolve(self_record.kernel);
    assert(resolved);
    assert(resolved.value() == self_record.peer);

    auto mismatched_credentials = self_record.kernel;
    ++mismatched_credentials.user_id;
    auto mismatch = registry.resolve(mismatched_credentials);
    assert(!mismatch);
    assert(mismatch.error().domain == os::core::ErrorDomain::security);
    assert(mismatch.error().code == os::core::errors::security::credential_mismatch);

    assert(registry.register_process(self_record, pidfd_for(::getpid())));
    assert(registry.size() == 1U);

    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        for (;;) ::pause();
    }

    const auto child_record = record_for(
        child, os::core::ProcessId{10U}, os::core::PrincipalId{0x30U, 0x40U}, os::core::UserId{5U});
    assert(registry.register_process(child_record, pidfd_for(child)));
    assert(registry.resolve(child_record.kernel));

    assert(::kill(child, SIGKILL) == 0);
    int status = 0;
    assert(::waitpid(child, &status, 0) == child);

    auto stale = registry.resolve(child_record.kernel);
    assert(!stale);
    assert(stale.error().domain == os::core::ErrorDomain::security);
    assert(stale.error().code == os::core::errors::security::stale_process);

    assert(registry.unregister_process(self_record.peer.process));
    auto missing = registry.resolve(self_record.kernel);
    assert(!missing);
    assert(missing.error().domain == os::core::ErrorDomain::security);
    assert(missing.error().code == os::core::errors::security::unknown_process);
    return 0;
}
