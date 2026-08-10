#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <utility>

#include <poll.h>

#include <os/accessibility/service.hpp>
#include <os/core/error.hpp>
#include <os/core/native_handle.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/constants.hpp>
#include <os/service/bootstrap.hpp>
#include <os/service/identity.hpp>

namespace {

[[nodiscard]] bool peer_died(const os::core::Error& error) noexcept {
    return error.domain == os::core::ErrorDomain::ipc &&
        error.code == os::ipc::errors::peer_died;
}

void report_fatal(const char* stage, const os::core::Error& error) noexcept {
    std::fprintf(
        stderr,
        "system.accessibility fatal stage=%s domain=%u code=%u\n",
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

    // This capability is supplied only by trusted Supervisor composition and
    // connects directly to App Manager's authenticated accessibility broker.
    auto broker_result = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::service::service_private_capability_fd});
    if (!broker_result) return 12;
    auto broker = std::move(broker_result).value();

    std::array<std::byte, os::ipc::max_wire_packet_size> bootstrap_buffer{};
    auto bootstrap = os::service::receive_bootstrap_request(
        control,
        bootstrap_buffer,
        os::accessibility::accessibility_service_id);
    if (!bootstrap) return 13;
    if (bootstrap.value().record.identity.principal !=
        os::accessibility::accessibility_service_principal) {
        return 14;
    }

    os::service::IdentityRegistry identities;
    os::accessibility::AccessibilityServiceRuntime runtime{
        broker,
        bootstrap.value().record.identity,
    };
    if (!runtime.valid()) return 15;
    os::accessibility::AccessibilityServiceServer service{runtime, identities};
    if (!service.valid()) return 16;

    auto ready = os::service::send_ready(control, bootstrap.value().request_header);
    if (!ready) return 17;

    std::array<std::byte, os::ipc::max_wire_packet_size> control_buffer{};
    std::array<std::byte, os::ipc::max_wire_packet_size> request_buffer{};

    for (;;) {
        std::array<pollfd, 2U> descriptors{
            pollfd{.fd = control.native_fd(), .events = POLLIN, .revents = 0},
            pollfd{.fd = endpoint.native_fd(), .events = POLLIN, .revents = 0},
        };

        int polled = -1;
        do {
            polled = ::poll(descriptors.data(), descriptors.size(), -1);
        } while (polled < 0 && errno == EINTR);
        if (polled < 0) return 18;

        if ((descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
            (descriptors[0].revents & POLLIN) == 0) {
            return 0;
        }
        if ((descriptors[0].revents & POLLIN) != 0) {
            auto handled = os::service::handle_identity_control_once(
                control,
                control_buffer,
                identities);
            if (!handled) {
                if (peer_died(handled.error())) return 0;
                report_fatal("control", handled.error());
                return 19;
            }
        }

        if ((descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
            (descriptors[1].revents & POLLIN) == 0) {
            return 0;
        }
        if ((descriptors[1].revents & POLLIN) != 0) {
            auto dispatched = service.dispatch_once(endpoint, request_buffer);
            if (!dispatched) {
                if (peer_died(dispatched.error())) return 0;
                // Stale/unregistered senders are request-local; the trusted
                // service remains alive for other Supervisor-owned clients.
                if (dispatched.error().domain == os::core::ErrorDomain::security) {
                    continue;
                }
                report_fatal("admin", dispatched.error());
                return 20;
            }
        }
    }
}
