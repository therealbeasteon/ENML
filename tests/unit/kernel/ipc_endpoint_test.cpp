#include <os/kernel/ipc_endpoint.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;

    constexpr ThreadId client = 10U;
    constexpr ThreadId server = 20U;
    constexpr ThreadId attacker = 30U;

    Rendezvous rendezvous{};
    require(rendezvous.create_thread(client));
    require(rendezvous.create_thread(server));
    require(rendezvous.create_thread(attacker));

    CapabilityTable capabilities{};
    IpcEndpointTable ipc{};

    auto endpoint = ipc.create(server);
    require(endpoint);
    require(ipc.active(endpoint.value()));
    require(ipc.active_endpoint_count() == 1U);

    const auto object = ipc_object_id(endpoint.value());
    require(object != invalid_object);
    auto server_cap = capabilities.mint(
        server, object, ipc_right_receive, false);
    auto client_cap = capabilities.mint(
        client, object, ipc_right_send, false);
    auto wrong_rights = capabilities.mint(
        attacker, object, ipc_right_receive, false);
    require(server_cap && client_cap && wrong_rights);

    // Capability possession, not endpoint/thread id knowledge, authorizes send.
    require(ipc.send(client, client_cap.value(), capabilities, rendezvous));
    require(ipc.pending_call_count() == 1U);
    auto received = ipc.receive(
        server, server_cap.value(), capabilities, rendezvous);
    require(received && received.value().valid());
    require(received.value().caller == client);
    require(ipc.active_reply_seal_count() == 1U);

    // Receive mints one-shot reply authority. Another server cannot use it and
    // the rightful server cannot replay it after successful consumption.
    require(!ipc.reply(attacker, received.value().reply, rendezvous));
    require(ipc.reply(server, received.value().reply, rendezvous));
    require(!ipc.reply(server, received.value().reply, rendezvous));
    require(ipc.pending_call_count() == 0U);
    require(ipc.active_reply_seal_count() == 0U);
    auto wake = rendezvous.wake_reason_of(client);
    require(wake && wake.value() == WakeReason::replied);

    // A receive-only capability cannot be used as send authority.
    require(!ipc.send(attacker, wrong_rights.value(), capabilities, rendezvous));

    // Receiver-first path: the blocked receive later recovers the exact caller
    // and can mint a reply seal without any kernel message queue.
    auto waiting = ipc.receive(server, server_cap.value(), capabilities, rendezvous);
    require(waiting && !waiting.value().valid());
    require(ipc.send(client, client_cap.value(), capabilities, rendezvous));
    auto delivered = ipc.receive(server, server_cap.value(), capabilities, rendezvous);
    require(delivered && delivered.value().valid());
    require(delivered.value().caller == client);

    const auto stale_seal = delivered.value().reply;
    require(ipc.pending_call_count() == 1U);

    // Retiring only this endpoint invalidates its reply seals, wakes its blocked
    // client, and leaves the server thread alive for other endpoints.
    require(ipc.retire(server, endpoint.value(), rendezvous));
    require(!ipc.active(endpoint.value()));
    require(ipc.pending_call_count() == 0U);
    require(ipc.active_reply_seal_count() == 0U);
    require(!ipc.reply(server, stale_seal, rendezvous));
    wake = rendezvous.wake_reason_of(client);
    require(wake && wake.value() == WakeReason::endpoint_retired);
    auto server_state = rendezvous.state_of(server);
    require(server_state && server_state.value() == ThreadState::ready);

    // Reusing the slot changes generation. Existing client/server capabilities
    // name the retired object and cannot silently attach to the replacement.
    auto replacement = ipc.create(server);
    require(replacement);
    require(replacement.value().slot == endpoint.value().slot);
    require(replacement.value().generation != endpoint.value().generation);
    require(!ipc.send(client, client_cap.value(), capabilities, rendezvous));
    require(!ipc.receive(server, server_cap.value(), capabilities, rendezvous));

    const auto replacement_object = ipc_object_id(replacement.value());
    auto replacement_server_cap = capabilities.mint(
        server, replacement_object, ipc_right_receive, false);
    auto replacement_client_cap = capabilities.mint(
        client, replacement_object, ipc_right_send, false);
    require(replacement_server_cap && replacement_client_cap);
    require(ipc.send(client, replacement_client_cap.value(), capabilities, rendezvous));
    auto replacement_request = ipc.receive(
        server, replacement_server_cap.value(), capabilities, rendezvous);
    require(replacement_request && replacement_request.value().valid());
    require(ipc.reply(server, replacement_request.value().reply, rendezvous));

    return 0;
}
