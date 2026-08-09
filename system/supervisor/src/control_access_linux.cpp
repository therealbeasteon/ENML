#include <os/supervisor/supervisor.hpp>

#include <cerrno>
#include <cstdint>

#include <fcntl.h>

#include <os/core/error.hpp>
#include <os/core/native_handle.hpp>

namespace os::supervisor {
namespace {

[[nodiscard]] constexpr os::core::Error service_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

} // namespace

os::core::Result<os::ipc::Channel>
Supervisor::connect_private_control() noexcept {
    if (state_ != ServiceState::running || !control_.valid()) {
        return service_error(os::core::errors::service::not_running);
    }

    int duplicate = -1;
    do {
        duplicate = ::fcntl(control_.native_fd(), F_DUPFD_CLOEXEC, 0);
    } while (duplicate < 0 && errno == EINTR);
    if (duplicate < 0) {
        return service_error(os::core::errors::service::launch_failed);
    }

    return os::ipc::Channel::adopt(os::core::NativeHandle{duplicate});
}

} // namespace os::supervisor
