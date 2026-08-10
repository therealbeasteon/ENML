#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>

#include <fcntl.h>
#include <poll.h>

#include <os/core/error.hpp>
#include <os/core/native_handle.hpp>
#include <os/core/platform_principals.hpp>
#include <os/display/buffer.hpp>
#include <os/display/service.hpp>
#include <os/display/shell_control.hpp>
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
        "system.compositor fatal stage=%s domain=%u code=%u\n",
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

    // M4.1 optionally composes one dedicated shell-control endpoint at the
    // Supervisor-private fd. It is never reachable through ServiceBroker or the
    // public compositor endpoint. When boot composition does not supply it, M3
    // behavior remains unchanged and no ambient shell-admin socket is created.
    os::ipc::Channel shell_endpoint{};
    errno = 0;
    const int private_fd_flags = ::fcntl(
        os::service::service_private_capability_fd,
        F_GETFD);
    if (private_fd_flags >= 0) {
        auto shell_result = os::ipc::Channel::adopt(
            os::core::NativeHandle{os::service::service_private_capability_fd});
        if (!shell_result) return 12;
        shell_endpoint = std::move(shell_result).value();
    } else if (errno != EBADF) {
        return 12;
    }

    std::array<std::byte, os::ipc::max_wire_packet_size> bootstrap_buffer{};
    auto bootstrap = os::service::receive_bootstrap_request(
        control,
        bootstrap_buffer,
        os::display::compositor_service_id);
    if (!bootstrap) return 13;
    const std::uint64_t service_generation = bootstrap.value().record.boot_generation;

    os::service::IdentityRegistry identities;
    os::display::Compositor compositor{
        os::display::DisplayConfiguration{
            .size = {1080U, 2400U},
            .safe_insets = {.top = 80U, .right = 0U, .bottom = 100U, .left = 0U},
            .refresh_millihz = 60'000U,
            .compositor_margin_ns = 1'000'000U,
        },
        os::display::TrustedUiPrincipals{
            .shell = os::core::shell_service_principal,
            .secure_ui = os::core::secure_ui_service_principal,
        },
        service_generation,
    };
    if (!compositor.valid()) return 14;

    os::display::SharedBufferPool buffers{service_generation};
    if (!buffers.valid()) return 15;
    os::display::CompositorService service{
        compositor,
        buffers,
        identities,
        os::display::input_service_principal,
    };
    os::display::ShellCompositorControlServer shell_service{
        os::display::shell_compositor_backend(compositor),
        identities,
    };
    if (!shell_service.valid()) return 16;

    auto ready = os::service::send_ready(control, bootstrap.value().request_header);
    if (!ready) return 17;

    std::array<std::byte, os::ipc::max_wire_packet_size> control_buffer{};
    std::array<std::byte, os::ipc::max_wire_packet_size> request_buffer{};
    std::array<std::byte, os::ipc::max_wire_packet_size> shell_buffer{};

    for (;;) {
        std::array<pollfd, 3U> descriptors{
            pollfd{.fd = control.native_fd(), .events = POLLIN, .revents = 0},
            pollfd{.fd = endpoint.native_fd(), .events = POLLIN, .revents = 0},
            pollfd{
                .fd = shell_endpoint.valid() ? shell_endpoint.native_fd() : -1,
                .events = shell_endpoint.valid() ? POLLIN : static_cast<short>(0),
                .revents = 0,
            },
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

        service.prune_dead_clients();

        if ((descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
            (descriptors[1].revents & POLLIN) == 0) {
            return 0;
        }
        if ((descriptors[1].revents & POLLIN) != 0) {
            auto dispatched = service.dispatch_once(endpoint, request_buffer);
            if (!dispatched) {
                if (peer_died(dispatched.error())) return 0;
                report_fatal("public", dispatched.error());
                return 20;
            }
        }

        if (shell_endpoint.valid()) {
            if ((descriptors[2].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
                (descriptors[2].revents & POLLIN) == 0) {
                // Private shell authority is generation-scoped. Losing its one
                // boot-composed peer disables this endpoint for the remainder
                // of the compositor generation rather than reopening or
                // discovering another shell client.
                shell_endpoint.close();
            } else if ((descriptors[2].revents & POLLIN) != 0) {
                auto dispatched = shell_service.dispatch_once(
                    shell_endpoint,
                    shell_buffer);
                if (!dispatched) {
                    if (peer_died(dispatched.error())) {
                        shell_endpoint.close();
                    } else if (dispatched.error().domain == os::core::ErrorDomain::security) {
                        // Unknown/stale senders are request-local. The private
                        // endpoint remains available to the live authenticated
                        // shell process already published by Supervisor.
                        continue;
                    } else {
                        report_fatal("shell-private", dispatched.error());
                        return 21;
                    }
                }
            }
        }
    }
}
