#include <os/app/manager.hpp>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <poll.h>

#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>

namespace os::app {
namespace {

[[nodiscard]] constexpr os::core::Error service_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

[[nodiscard]] constexpr os::core::Error security_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::security, code);
}

[[nodiscard]] bool is_peer_died(const os::core::Error& error) noexcept {
    return error.domain == os::core::ErrorDomain::ipc &&
        error.code == os::ipc::errors::peer_died;
}

void close_on_send_failure(
    os::ipc::Channel& session,
    const os::core::Result<void>& result) noexcept {
    if (!result) session.close();
}

} // namespace

bool ApplicationManager::service_allowed(
    const InstanceSlot& slot,
    os::core::ServiceId service) const noexcept {
    if (service.value() == 0U || slot.service_count > slot.services.size()) return false;
    for (std::size_t index = 0U; index < slot.service_count; ++index) {
        if (slot.services[index] == service) return true;
    }
    return false;
}

os::core::Result<void>
ApplicationManager::service_runtime_session_once(InstanceSlot& slot) noexcept {
    if (!slot.occupied || !slot.info.valid() || !slot.service_session.valid()) return {};
    if (service_broker_ == nullptr || !broker_configuration_valid()) {
        slot.service_session.close();
        return service_error(manager_errors::broker_misconfigured);
    }

    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto request = receive_service_acquire_request(slot.service_session, scratch);
    if (!request) {
        // A malformed packet or a dead peer is contained to this application's
        // private runtime session. It must not destabilize App Manager or any
        // other application's service connectivity.
        slot.service_session.close();
        return {};
    }

    auto authoritative = service_broker_->lookup(slot.info.identity.process);
    if (!authoritative) {
        auto sent = os::ipc::send_rpc_error(
            slot.service_session,
            request.value().request_header,
            authoritative.error());
        close_on_send_failure(slot.service_session, sent);
        return {};
    }

    // SCM_CREDENTIALS is the evidence for the actual packet sender. Match all
    // native credentials and the already-bound PeerIdentity; payload data never
    // gets to claim a ProcessId/PrincipalId/UserId.
    if (authoritative.value().peer != slot.info.identity ||
        authoritative.value().kernel != request.value().sender) {
        auto sent = os::ipc::send_rpc_error(
            slot.service_session,
            request.value().request_header,
            security_error(os::core::errors::security::credential_mismatch));
        close_on_send_failure(slot.service_session, sent);
        return {};
    }

    // The bootstrap service set is the application-visible allow-list for this
    // session. ServiceBroker independently enforces the same process/service
    // attachment, so a forged or future ServiceId cannot expand authority.
    if (!service_allowed(slot, request.value().service)) {
        auto sent = os::ipc::send_rpc_error(
            slot.service_session,
            request.value().request_header,
            service_error(os::core::errors::service::access_denied));
        close_on_send_failure(slot.service_session, sent);
        return {};
    }

    auto endpoint = service_broker_->connect_current(
        slot.info.identity.process,
        request.value().service);
    if (!endpoint) {
        auto sent = os::ipc::send_rpc_error(
            slot.service_session,
            request.value().request_header,
            endpoint.error());
        close_on_send_failure(slot.service_session, sent);
        return {};
    }

    // known_generation is deliberately advisory. The trusted broker decides
    // which supervised generation is current; an application cannot request an
    // older implementation or fabricate a future generation as authority.
    auto brokered = std::move(endpoint).value();
    auto native_endpoint = brokered.channel.take_native_handle_for_transfer();
    auto sent = send_service_acquire_response(
        slot.service_session,
        request.value().request_header,
        brokered.service,
        brokered.generation,
        native_endpoint);
    close_on_send_failure(slot.service_session, sent);
    return {};
}

os::core::Result<void>
ApplicationManager::service_runtime_session_if_pending(InstanceSlot& slot) noexcept {
    if (!slot.occupied || !slot.service_session.valid()) return {};

    // Bound work per maintain() iteration. A chatty application cannot make the
    // system lifecycle loop spend unbounded time servicing endpoint requests.
    for (std::size_t attempt = 0U; attempt < 4U; ++attempt) {
        pollfd descriptor{
            .fd = slot.service_session.native_fd(),
            .events = POLLIN,
            .revents = 0,
        };
        int result = -1;
        do {
            result = ::poll(&descriptor, 1, 0);
        } while (result < 0 && errno == EINTR);

        if (result < 0) {
            slot.service_session.close();
            return {};
        }
        if (result == 0) return {};

        if ((descriptor.revents & POLLIN) != 0) {
            auto serviced = service_runtime_session_once(slot);
            if (!serviced) {
                if (is_peer_died(serviced.error())) {
                    slot.service_session.close();
                    return {};
                }
                return serviced.error();
            }
            if (!slot.service_session.valid()) return {};
            continue;
        }

        if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            slot.service_session.close();
            return {};
        }
        return {};
    }
    return {};
}

} // namespace os::app
