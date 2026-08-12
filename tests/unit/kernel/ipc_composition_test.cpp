#include <cstdlib>

#include <os/kernel/kernel.hpp>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    constexpr os::kernel::ThreadId client = 11U;
    constexpr os::kernel::ThreadId server = 22U;

    os::kernel::Kernel kernel{};
    require(kernel.create_thread(client, 4U));
    require(kernel.create_thread(server, 2U));

    auto endpoint = kernel.create_ipc_endpoint(server);
    require(endpoint);

    auto owner_cap = kernel.capabilities().mint(
        server,
        os::kernel::ipc_object_id(endpoint.value()),
        os::kernel::ipc_right_all,
        true);
    require(owner_cap);
    auto client_cap = kernel.capabilities().grant(
        server,
        owner_cap.value(),
        client,
        os::kernel::ipc_right_send,
        false);
    require(client_cap);

    // Send blocks the client through the composed Kernel and scheduler state is
    // synchronized immediately; no caller has to remember a separate update.
    require(kernel.ipc_send(client, client_cap.value()));
    auto client_runnable = kernel.runqueue().is_runnable(client);
    require(client_runnable && !client_runnable.value());

    auto received = kernel.ipc_receive(server, owner_cap.value());
    require(received && received.value().valid());
    require(received.value().caller == client);

    require(kernel.ipc_reply(server, received.value().reply));
    client_runnable = kernel.runqueue().is_runnable(client);
    require(client_runnable && client_runnable.value());

    // A second call is cancelled by endpoint retirement. The client becomes
    // runnable with endpoint_retired, while the server thread itself survives.
    require(kernel.ipc_send(client, client_cap.value()));
    require(kernel.retire_ipc_endpoint(server, endpoint.value()));
    auto wake = kernel.threads().wake_reason_of(client);
    require(wake && wake.value() == os::kernel::WakeReason::endpoint_retired);
    client_runnable = kernel.runqueue().is_runnable(client);
    require(client_runnable && client_runnable.value());
    require(kernel.live_thread_count() == 2U);

    // Thread death also retires every endpoint owned by the dying server before
    // generic rendezvous teardown, so endpoint lifecycle cannot be forgotten.
    auto endpoint2 = kernel.create_ipc_endpoint(server);
    require(endpoint2);
    auto owner2 = kernel.capabilities().mint(
        server,
        os::kernel::ipc_object_id(endpoint2.value()),
        os::kernel::ipc_right_all,
        true);
    require(owner2);
    auto client2 = kernel.capabilities().grant(
        server,
        owner2.value(),
        client,
        os::kernel::ipc_right_send,
        false);
    require(client2);
    require(kernel.ipc_send(client, client2.value()));

    auto teardown = kernel.destroy_thread(server);
    require(teardown);
    require(teardown.value().ipc_endpoints_retired == 1U);
    require(!kernel.ipc().active(endpoint2.value()));
    wake = kernel.threads().wake_reason_of(client);
    require(wake && wake.value() == os::kernel::WakeReason::endpoint_retired);
    require(kernel.live_thread_count() == 1U);

    return 0;
}
