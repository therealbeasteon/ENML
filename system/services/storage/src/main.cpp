#include <array>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <utility>

#include <poll.h>

#include <os/core/error.hpp>
#include <os/core/native_handle.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/constants.hpp>
#include <os/service/bootstrap.hpp>
#include <os/service/identity.hpp>
#include <os/storage/service.hpp>

namespace {

[[nodiscard]] bool peer_died(const os::core::Error& error) noexcept {
    return error.domain == os::core::ErrorDomain::ipc &&
        error.code == os::ipc::errors::peer_died;
}

void report_fatal(const char* stage, const os::core::Error& error) noexcept {
    std::fprintf(
        stderr,
        "system.storage fatal stage=%s domain=%u code=%u\n",
        stage,
        static_cast<unsigned>(error.domain),
        static_cast<unsigned>(error.code));
}

} // namespace

int main() {
    auto control_result = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::service::bootstrap_control_fd});
    if (!control_result) return 10;
    auto control = std::move(control_result).value();

    auto endpoint_result = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::service::service_endpoint_fd});
    if (!endpoint_result) return 11;
    auto endpoint = std::move(endpoint_result).value();

    std::array<std::byte, os::ipc::max_wire_packet_size> bootstrap_buffer{};
    auto bootstrap = os::service::receive_bootstrap_request(
        control, bootstrap_buffer, os::storage::storage_service_id);
    if (!bootstrap) return 12;

    os::service::IdentityRegistry identities;
    os::storage::PrivateRootRegistry roots;
    os::storage::StorageService service{endpoint, identities, roots};

    auto ready = os::service::send_ready(control, bootstrap.value().request_header);
    if (!ready) return 13;

    std::array<std::byte, os::ipc::max_wire_packet_size> control_buffer{};
    std::array<std::byte, os::ipc::max_wire_packet_size> request_buffer{};

    for (;;) {
        pollfd control_poll{.fd = control.native_fd(), .events = POLLIN, .revents = 0};
        int polled = -1;
        do {
            polled = ::poll(&control_poll, 1, 0);
        } while (polled < 0 && errno == EINTR);
        if (polled < 0) return 14;

        if ((control_poll.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
            (control_poll.revents & POLLIN) == 0) {
            return 0;
        }
        if ((control_poll.revents & POLLIN) != 0) {
            auto handled = service.dispatch_control_once(control, control_buffer, identities);
            if (!handled) {
                if (peer_died(handled.error())) return 0;
                report_fatal("control", handled.error());
                return 15;
            }
        }

        auto dispatched = service.dispatch_once(request_buffer, 10);
        if (!dispatched) {
            if (peer_died(dispatched.error())) return 0;
            report_fatal("public", dispatched.error());
            return 16;
        }
    }
}
