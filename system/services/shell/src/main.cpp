#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <utility>

#include <poll.h>

#include <os/app/shell_lifecycle_client.hpp>
#include <os/core/error.hpp>
#include <os/core/native_handle.hpp>
#include <os/core/platform_principals.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/constants.hpp>
#include <os/service/bootstrap.hpp>
#include <os/service/identity.hpp>
#include <os/shell/service.hpp>

namespace {

[[nodiscard]] bool peer_died(const os::core::Error& error) noexcept {
    return error.domain == os::core::ErrorDomain::ipc &&
        error.code == os::ipc::errors::peer_died;
}

void report_fatal(const char* stage, const os::core::Error& error) noexcept {
    std::fprintf(
        stderr,
        "system.shell fatal stage=%s domain=%u code=%u\n",
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

    // M4.1 intentionally exposes no application-facing shell-admin service.
    // Supervisor still supplies its standard service endpoint at fd 4, so
    // adopt and close it before READY rather than accidentally turning it into
    // a future ambient authority path.
    auto endpoint_result = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::service::service_endpoint_fd});
    if (!endpoint_result) return 11;
    auto endpoint = std::move(endpoint_result).value();
    endpoint.close();

    // The first shell-private capability is an authenticated App Manager
    // lifecycle/bootstrap endpoint. It is injected by trusted composition only
    // and is never obtainable through Supervisor::connect() or ServiceBroker.
    auto lifecycle_result = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::service::service_private_capability_fd});
    if (!lifecycle_result) return 12;
    auto lifecycle = std::move(lifecycle_result).value();

    std::array<std::byte, os::ipc::max_wire_packet_size> bootstrap_buffer{};
    auto bootstrap = os::service::receive_bootstrap_request(
        control,
        bootstrap_buffer,
        os::shell::shell_service_id);
    if (!bootstrap) return 13;
    if (bootstrap.value().record.identity.principal !=
        os::core::shell_service_principal) {
        return 14;
    }

    os::service::IdentityRegistry identities;

    // READY certifies only that the shell process has accepted its Supervisor
    // identity and required private bootstrap capability. The first authority
    // acquisitions happen immediately afterward so startup remains fail-closed
    // without forcing Supervisor::start() to depend on external dispatch.
    auto ready = os::service::send_ready(control, bootstrap.value().request_header);
    if (!ready) return 15;

    os::app::ShellLifecycleControlClient lifecycle_client{lifecycle};
    std::array<std::byte, os::ipc::max_wire_packet_size> lifecycle_buffer{};
    auto initial_lifecycle = lifecycle_client.snapshot(lifecycle_buffer);
    if (!initial_lifecycle || initial_lifecycle.value().revision == 0U) {
        if (!initial_lifecycle) report_fatal("lifecycle", initial_lifecycle.error());
        return 16;
    }

    // App Manager/boot composition transfers exactly one private compositor
    // control channel after authenticating this live shell sender. The eventual
    // compositor server re-authenticates the shell on every privileged request,
    // so the descriptor is narrow composition, not a bearer-only privilege.
    auto compositor_result = lifecycle_client.take_compositor_capability(
        lifecycle_buffer);
    if (!compositor_result) {
        report_fatal("compositor-capability", compositor_result.error());
        return 17;
    }
    auto compositor_control = std::move(compositor_result).value();
    if (!compositor_control.valid()) return 18;

    // No periodic task scan, process scan, thumbnail refresh, animation timer
    // or polling timeout is introduced here. The shell sleeps until trusted
    // Supervisor control traffic arrives; later navigation/input sources will
    // be added as explicit event descriptors.
    std::array<std::byte, os::ipc::max_wire_packet_size> control_buffer{};
    for (;;) {
        pollfd descriptor{
            .fd = control.native_fd(),
            .events = POLLIN,
            .revents = 0,
        };

        int polled = -1;
        do {
            polled = ::poll(&descriptor, 1U, -1);
        } while (polled < 0 && errno == EINTR);
        if (polled < 0) return 19;

        if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
            (descriptor.revents & POLLIN) == 0) {
            return 0;
        }
        if ((descriptor.revents & POLLIN) != 0) {
            auto handled = os::service::handle_identity_control_once(
                control,
                control_buffer,
                identities);
            if (!handled) {
                if (peer_died(handled.error())) return 0;
                report_fatal("control", handled.error());
                return 20;
            }
        }
    }
}
