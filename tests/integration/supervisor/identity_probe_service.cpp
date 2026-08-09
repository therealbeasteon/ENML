#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <poll.h>

#include <os/core/error.hpp>
#include <os/core/native_handle.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/encoder.hpp>
#include <os/ipc/rpc.hpp>
#include <os/service/bootstrap.hpp>
#include <os/service/identity.hpp>

#ifndef PROBE_SERVICE_ID
#error "PROBE_SERVICE_ID must be defined"
#endif

namespace {

constexpr os::core::ServiceId probe_service_id{static_cast<std::uint32_t>(PROBE_SERVICE_ID)};
constexpr std::uint32_t identify_operation = 1U;

[[nodiscard]] os::core::Result<void>
dispatch_public_once(
    os::ipc::Channel& endpoint,
    os::service::IdentityRegistry& identities,
    os::core::MutableByteSpan receive_buffer) noexcept {
    auto received = endpoint.receive(receive_buffer);
    if (!received) return received.error();
    auto message = std::move(received).value();

    auto context = os::ipc::validate_rpc_request(message, probe_service_id, identities);
    if (!context) {
        return os::ipc::send_rpc_error(endpoint, message.header(), context.error());
    }
    if (message.header().operation_id != identify_operation || !message.payload().empty()) {
        return os::ipc::send_rpc_error(
            endpoint,
            message.header(),
            os::core::make_error(
                os::core::ErrorDomain::service,
                os::core::errors::service::unknown_operation));
    }

    std::array<std::byte, 32U> payload{};
    os::ipc::Encoder encoder{payload};
    auto encoded = encoder.write_u64_le(context.value().peer.process.value());
    if (!encoded) return encoded.error();
    encoded = encoder.write_u64_le(context.value().peer.principal.high);
    if (!encoded) return encoded.error();
    encoded = encoder.write_u64_le(context.value().peer.principal.low);
    if (!encoded) return encoded.error();
    encoded = encoder.write_u64_le(context.value().peer.user.value());
    if (!encoded) return encoded.error();
    return os::ipc::send_rpc_response(endpoint, message.header(), encoder.written());
}

} // namespace

int main() {
    auto control_result = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::service::bootstrap_control_fd});
    auto endpoint_result = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::service::service_endpoint_fd});
    if (!control_result || !endpoint_result) return 10;
    auto control = std::move(control_result).value();
    auto endpoint = std::move(endpoint_result).value();

    std::array<std::byte, os::ipc::max_wire_packet_size> bootstrap_buffer{};
    auto bootstrap = os::service::receive_bootstrap_request(
        control,
        bootstrap_buffer,
        probe_service_id);
    if (!bootstrap) return 11;

    os::service::IdentityRegistry identities;
    auto ready = os::service::send_ready(control, bootstrap.value().request_header);
    if (!ready) return 12;

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
        if (polled < 0) return 13;

        if ((descriptors[0].revents & POLLIN) != 0) {
            auto handled = os::service::handle_identity_control_once(
                control,
                control_buffer,
                identities);
            if (!handled) {
                if (handled.error().domain == os::core::ErrorDomain::ipc &&
                    handled.error().code == os::ipc::errors::peer_died) {
                    return 0;
                }
                return 14;
            }
        }
        if ((descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
            (descriptors[0].revents & POLLIN) == 0) {
            return 0;
        }

        if ((descriptors[1].revents & POLLIN) != 0) {
            auto dispatched = dispatch_public_once(endpoint, identities, request_buffer);
            if (!dispatched) {
                if (dispatched.error().domain == os::core::ErrorDomain::ipc &&
                    dispatched.error().code == os::ipc::errors::peer_died) {
                    return 0;
                }
                return 15;
            }
        }
        if ((descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
            (descriptors[1].revents & POLLIN) == 0) {
            return 0;
        }
    }
}
