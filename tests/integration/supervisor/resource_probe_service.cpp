#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

#include <os/core/native_handle.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/constants.hpp>
#include <os/service/bootstrap.hpp>

namespace {

[[nodiscard]] bool check_file_size_limit() {
    struct sigaction action{};
    action.sa_handler = SIG_IGN;
    if (::sigemptyset(&action.sa_mask) != 0 || ::sigaction(SIGXFSZ, &action, nullptr) != 0) {
        return false;
    }

    char path[128]{};
    const int length = std::snprintf(
        path, sizeof(path), "/tmp/emnl-m09-fsize-%ld.tmp", static_cast<long>(::getpid()));
    if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(path)) return false;

    const int fd = ::open(path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    (void)::unlink(path);

    std::array<std::byte, 1024U> block{};
    std::size_t written = 0U;
    bool saw_limit = false;
    while (written <= 8192U) {
        errno = 0;
        const ssize_t result = ::write(fd, block.data(), block.size());
        if (result < 0) {
            saw_limit = errno == EFBIG;
            break;
        }
        written += static_cast<std::size_t>(result);
    }
    (void)::close(fd);
    return saw_limit && written == 4096U;
}

[[nodiscard]] bool check_descriptor_limit() {
    std::array<int, 32U> descriptors{};
    descriptors.fill(-1);
    std::size_t opened = 0U;
    bool saw_limit = false;

    for (; opened < descriptors.size(); ++opened) {
        errno = 0;
        const int fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            saw_limit = errno == EMFILE;
            break;
        }
        descriptors[opened] = fd;
    }

    for (const int fd : descriptors) {
        if (fd >= 0) (void)::close(fd);
    }

    return saw_limit && opened <= 7U;
}

} // namespace

int main() {
    rlimit limit{};
    if (::getrlimit(RLIMIT_NOFILE, &limit) != 0 || limit.rlim_cur != 12U) return 20;
    if (::getrlimit(RLIMIT_FSIZE, &limit) != 0 || limit.rlim_cur != 4096U) return 21;
    if (::getrlimit(RLIMIT_CORE, &limit) != 0 || limit.rlim_cur != 0U) return 22;

    if (!check_file_size_limit()) return 23;
    if (!check_descriptor_limit()) return 24;

    auto control_result = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::service::bootstrap_control_fd});
    if (!control_result) return 25;
    auto control = std::move(control_result).value();

    std::array<std::byte, os::ipc::max_wire_packet_size> buffer{};
    constexpr os::core::ServiceId service_id{0xFFFF0902U};
    auto bootstrap_result = os::service::receive_bootstrap_request(control, buffer, service_id);
    if (!bootstrap_result) return 26;
    if (!os::service::send_ready(control, bootstrap_result.value().request_header)) return 27;

    for (;;) {
        pollfd descriptor{.fd = control.native_fd(), .events = POLLIN, .revents = 0};
        int result = -1;
        do {
            result = ::poll(&descriptor, 1, -1);
        } while (result < 0 && errno == EINTR);
        if (result < 0) return 28;
        if ((descriptor.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) return 0;
    }
}
