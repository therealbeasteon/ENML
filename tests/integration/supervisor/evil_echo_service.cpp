#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <fcntl.h>
#include <linux/capability.h>
#include <linux/seccomp.h>
#include <poll.h>
#include <sched.h>
#include <sys/ptrace.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <os/core/native_handle.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/constants.hpp>
#include <os/service/bootstrap.hpp>

namespace {

[[nodiscard]] bool capabilities_are_empty() noexcept {
#if defined(SYS_capget)
    __user_cap_header_struct header{};
    header.version = _LINUX_CAPABILITY_VERSION_3;
    header.pid = 0;
    std::array<__user_cap_data_struct, 2> data{};
    if (::syscall(SYS_capget, &header, data.data()) != 0) return false;
    for (const auto& item : data) {
        if (item.effective != 0U || item.permitted != 0U || item.inheritable != 0U) {
            return false;
        }
    }
    return true;
#else
    return false;
#endif
}

[[nodiscard]] bool limits_are_bounded() noexcept {
    rlimit value{};
    if (::getrlimit(RLIMIT_CORE, &value) != 0 || value.rlim_cur != 0U) return false;
    if (::getrlimit(RLIMIT_NOFILE, &value) != 0 || value.rlim_cur > 32U) return false;
    if (::getrlimit(RLIMIT_NPROC, &value) != 0 || value.rlim_cur > 8U) return false;
    if (::getrlimit(RLIMIT_FSIZE, &value) != 0 || value.rlim_cur > 1024U * 1024U) return false;
    return true;
}

[[nodiscard]] bool extra_fds_are_closed() noexcept {
    for (int fd = 5; fd < 32; ++fd) {
        errno = 0;
        if (::fcntl(fd, F_GETFD) >= 0 || errno != EBADF) return false;
    }
    return true;
}

} // namespace

int main() {
    if (::prctl(PR_GET_NO_NEW_PRIVS, 0L, 0L, 0L, 0L) != 1) return 20;
    if (::prctl(PR_GET_SECCOMP, 0L, 0L, 0L, 0L) != SECCOMP_MODE_FILTER) return 21;
    int parent_death_signal = 0;
    if (::prctl(PR_GET_PDEATHSIG, &parent_death_signal, 0L, 0L, 0L) != 0 ||
        parent_death_signal != SIGKILL) return 31;
    if (!capabilities_are_empty()) return 22;
    if (!limits_are_bounded()) return 23;
    if (std::getenv("EMNL_SANDBOX_SECRET") != nullptr) return 24;
    if (!extra_fds_are_closed()) return 25;

    errno = 0;
    if (::unshare(CLONE_FILES) != -1 || errno != EPERM) return 30;

#if defined(SYS_setns)
    errno = 0;
    if (::syscall(SYS_setns, -1, 0) != -1 || errno != EPERM) return 32;
#endif
#if defined(SYS_ptrace)
    errno = 0;
    if (::syscall(SYS_ptrace, PTRACE_TRACEME, 0, 0, 0) != -1 || errno != EPERM) return 33;
#endif
#if defined(SYS_open_by_handle_at)
    errno = 0;
    if (::syscall(SYS_open_by_handle_at, -1, nullptr, 0) != -1 || errno != EPERM) return 34;
#endif

    auto control_result = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::service::bootstrap_control_fd});
    if (!control_result) return 26;
    auto control = std::move(control_result).value();

    std::array<std::byte, os::ipc::max_wire_packet_size> buffer{};
    constexpr os::core::ServiceId probe_service_id{0xFFFF00E8U};
    auto bootstrap_result = os::service::receive_bootstrap_request(
        control, buffer, probe_service_id);
    if (!bootstrap_result) return 27;

    auto ready_result = os::service::send_ready(control, bootstrap_result.value().request_header);
    if (!ready_result) return 28;

    for (;;) {
        pollfd descriptor{.fd = control.native_fd(), .events = POLLIN, .revents = 0};
        int result = -1;
        do {
            result = ::poll(&descriptor, 1, -1);
        } while (result < 0 && errno == EINTR);
        if (result < 0) return 29;
        if ((descriptor.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) return 0;
    }
}
