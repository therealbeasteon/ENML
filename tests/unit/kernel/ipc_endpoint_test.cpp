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

    require(ipc.send(client, client_cap.value(), capabilities, rendezvous));
    require(ipc.pending_call_count() == 1U);
    auto received = ipc.receive(
        server, server_cap.value(), capabilities, rendezvous);
    require(received && received.value().valid());
    require(received.value().caller == client);
    require(ipc.active_reply_seal_count() == 1U);

    require(!ipc.reply(attacker, received.value().reply, rendezvous));
    require(ipc.reply(server, received.value().reply, rendezvous));
    require(!ipc.reply(server, received.value().reply, rendezvous));
    require(ipc.pending_call_count() == 0U);
    require(ipc.active_reply_seal_count() == 0U);
    auto wake = rendezvous.wake_reason_of(client);
    require(wake && wake.value() == WakeReason::replied);

    // Completed replies are consumed exactly once. A caller cannot begin a new
    // synchronous call while an earlier response is still parked in-kernel.
    require(!ipc.send(client, client_cap.value(), capabilities, rendezvous));
    require(ipc.take_reply(client));
    require(!ipc.take_reply(client));

    require(!ipc.send(attacker, wrong_rights.value(), capabilities, rendezvous));

    auto waiting = ipc.receive(server, server_cap.value(), capabilities, rendezvous);
    require(waiting && !waiting.value().valid());
    require(ipc.send(client, client_cap.value(), capabilities, rendezvous));
    auto delivered = ipc.receive(server, server_cap.value(), capabilities, rendezvous);
    require(delivered && delivered.value().valid());
    require(delivered.value().caller == client);

    const auto stale_seal = delivered.value().reply;
    require(ipc.pending_call_count() == 1U);

    require(ipc.retire(server, endpoint.value(), rendezvous));
    require(!ipc.active(endpoint.value()));
    require(ipc.pending_call_count() == 0U);
    require(ipc.active_reply_seal_count() == 0U);
    require(!ipc.reply(server, stale_seal, rendezvous));
    wake = rendezvous.wake_reason_of(client);
    require(wake && wake.value() == WakeReason::endpoint_retired);
    require(!ipc.take_reply(client));
    auto server_state = rendezvous.state_of(server);
    require(server_state && server_state.value() == ThreadState::ready);

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
    require(ipc.take_reply(client));

    // One server may own multiple endpoints, but a blocked receive is bound to
    // exactly one endpoint generation. Traffic to B must not satisfy a receive
    // waiting on A, even though both endpoints share the same server thread.
    auto endpoint_b = ipc.create(server);
    require(endpoint_b);
    const auto object_b = ipc_object_id(endpoint_b.value());
    auto server_cap_b = capabilities.mint(
        server, object_b, ipc_right_receive, false);
    auto attacker_cap_b = capabilities.mint(
        attacker, object_b, ipc_right_send, false);
    require(server_cap_b && attacker_cap_b);

    auto wait_a = ipc.receive(
        server, replacement_server_cap.value(), capabilities, rendezvous);
    require(wait_a && !wait_a.value().valid());
    server_state = rendezvous.state_of(server);
    require(server_state && server_state.value() == ThreadState::receive_blocked);

    require(ipc.send(attacker, attacker_cap_b.value(), capabilities, rendezvous));
    server_state = rendezvous.state_of(server);
    require(server_state && server_state.value() == ThreadState::receive_blocked);
    auto attacker_state = rendezvous.state_of(attacker);
    require(attacker_state && attacker_state.value() == ThreadState::send_blocked);

    require(ipc.send(client, replacement_client_cap.value(), capabilities, rendezvous));
    server_state = rendezvous.state_of(server);
    require(server_state && server_state.value() == ThreadState::ready);

    auto only_a = ipc.receive(
        server, replacement_server_cap.value(), capabilities, rendezvous);
    require(only_a && only_a.value().valid());
    require(only_a.value().caller == client);
    require(ipc.reply(server, only_a.value().reply, rendezvous));
    require(ipc.take_reply(client));

    auto then_b = ipc.receive(server, server_cap_b.value(), capabilities, rendezvous);
    require(then_b && then_b.value().valid());
    require(then_b.value().caller == attacker);
    require(ipc.reply(server, then_b.value().reply, rendezvous));
    require(ipc.take_reply(attacker));

    return 0;
}
