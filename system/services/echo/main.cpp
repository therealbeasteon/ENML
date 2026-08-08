#include <array>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <cstdlib>

#include <poll.h>

#include <os/core/native_handle.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/channel.hpp>
#include <os/service/bootstrap.hpp>
#include <os/service/identity.hpp>

#include "echo_server.generated.hpp"

namespace generated = os::test::echo;

namespace {

[[nodiscard]] generated::IdentityResponse identity_response(const os::ipc::RequestContext& context) noexcept {
    return generated::IdentityResponse{
        .process_id = context.peer.process.value(),
        .principal_high = context.peer.principal.high,
        .principal_low = context.peer.principal.low,
        .user_id = context.peer.user.value(),
    };
}

class EchoImplementation final : public generated::EchoServer {
public:
    explicit EchoImplementation(os::core::PeerIdentity self_identity) noexcept
        : self_identity_(self_identity) {}

    os::core::Result<generated::PingResponse>
    ping(const os::ipc::RequestContext&, const generated::PingRequest& request) noexcept override {
        return generated::PingResponse{.text = request.text};
    }

    os::core::Result<generated::IdentityResponse>
    identify(const os::ipc::RequestContext& context, const generated::Empty&) noexcept override {
        (void)self_identity_;
        return identity_response(context);
    }

    os::core::Result<generated::IdentityResponse>
    identify_claim(
        const os::ipc::RequestContext& context,
        const generated::IdentityClaimRequest&) noexcept override {
        // The claim is deliberately ignored. Identity is exclusively supplied
        // by RequestContext after kernel credentials are resolved through the
        // supervisor-published registry.
        return identity_response(context);
    }

private:
    os::core::PeerIdentity self_identity_ {};
};

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
    auto bootstrap_result = os::service::receive_bootstrap_request(
        control, bootstrap_buffer, generated::EchoDispatcher::service_id);
    if (!bootstrap_result) return 12;
    const auto bootstrap = bootstrap_result.value();

    os::service::IdentityRegistry identity_registry;
    EchoImplementation implementation{bootstrap.record.identity};
    generated::EchoDispatcher dispatcher{implementation, endpoint, identity_registry};

    auto ready_result = os::service::send_ready(control, bootstrap.request_header);
    if (!ready_result) return 13;

    std::array<std::byte, os::ipc::max_wire_packet_size> control_buffer{};
    std::array<std::byte, os::ipc::max_wire_packet_size> request_buffer{};

    for (;;) {
        std::array<pollfd, 2> descriptors{
            pollfd{.fd = control.native_fd(), .events = POLLIN, .revents = 0},
            pollfd{.fd = endpoint.native_fd(), .events = POLLIN, .revents = 0},
        };

        int poll_result = -1;
        do {
            poll_result = ::poll(descriptors.data(), descriptors.size(), -1);
        } while (poll_result < 0 && errno == EINTR);
        if (poll_result < 0) return 14;

        if ((descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            return 0;
        }
        if ((descriptors[0].revents & POLLIN) != 0) {
            auto control_dispatch = os::service::handle_identity_control_once(
                control, control_buffer, identity_registry);
            if (!control_dispatch) {
                if (control_dispatch.error().domain == os::core::ErrorDomain::ipc &&
                    control_dispatch.error().code == os::ipc::errors::peer_died) {
                    return 0;
                }
                return 15;
            }
        }

        if ((descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
            (descriptors[1].revents & POLLIN) == 0) {
            return 0;
        }
        if ((descriptors[1].revents & POLLIN) != 0) {
            auto dispatch_result = dispatcher.dispatch_one(request_buffer);
            if (!dispatch_result) {
                if (dispatch_result.error().domain == os::core::ErrorDomain::ipc &&
                    dispatch_result.error().code == os::ipc::errors::peer_died) {
                    return 0;
                }
                // Security failures for an unregistered/stale sender are
                // request-local. The service stays alive for other clients.
                if (dispatch_result.error().domain == os::core::ErrorDomain::security) {
                    continue;
                }
                return 16;
            }
        }
    }
}
