#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>

#include <poll.h>

#include <os/core/error.hpp>
#include <os/core/native_handle.hpp>
#include <os/display/buffer.hpp>
#include <os/display/service.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/constants.hpp>
#include <os/service/bootstrap.hpp>
#include <os/service/identity.hpp>

namespace {

constexpr os::core::PrincipalId shell_principal{
    0x454E4D4C5348454CULL,
    0x4C00000000000001ULL,
};
constexpr os::core::PrincipalId secure_ui_principal{
    0x454E4D4C53454355ULL,
    0x5245554900000001ULL,
};

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

    std::array<std::byte, os::ipc::max_wire_packet_size> bootstrap_buffer{};
    auto bootstrap = os::service::receive_bootstrap_request(
        control,
        bootstrap_buffer,
        os::display::compositor_service_id);
    if (!bootstrap) return 12;
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
            .shell = shell_principal,
            .secure_ui = secure_ui_principal,
        },
        service_generation,
    };
    if (!compositor.valid()) return 13;

    os::display::SharedBufferPool buffers{service_generation};
    if (!buffers.valid()) return 14;
    os::display::CompositorService service{
        compositor,
        buffers,
        identities,
        os::display::input_service_principal,
    };

    auto ready = os::service::send_ready(control, bootstrap.value().request_header);
    if (!ready) return 15;

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
        if (polled < 0) return 16;

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
                return 17;
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
                return 18;
            }
        }
    }
}
