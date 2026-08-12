#include <array>
#include <cstddef>
#include <cstdlib>
#include <span>

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

    const std::array<std::byte, 4U> request_bytes{
        std::byte{0x43}, std::byte{0x4F}, std::byte{0x4F}, std::byte{0x4B}};
    auto request = os::kernel::IpcEnvelope::from(std::span<const std::byte>{request_bytes});
    require(request);

    require(kernel.ipc_send(client, client_cap.value(), request.value()));
    auto client_runnable = kernel.runqueue().is_runnable(client);
    require(client_runnable && !client_runnable.value());

    auto received = kernel.ipc_receive(server, owner_cap.value());
    require(received && received.value().valid());
    require(received.value().caller == client);
    require(received.value().request == request.value());

    const std::array<std::byte, 2U> reply_bytes{std::byte{0x4F}, std::byte{0x4B}};
    auto response = os::kernel::IpcEnvelope::from(std::span<const std::byte>{reply_bytes});
    require(response);
    require(kernel.ipc_reply(server, received.value().reply, response.value()));
    client_runnable = kernel.runqueue().is_runnable(client);
    require(client_runnable && client_runnable.value());

    auto collected = kernel.ipc_take_reply(client);
    require(collected && collected.value() == response.value());
    require(!kernel.ipc_take_reply(client));

    // A second call is cancelled by endpoint retirement. No completed response
    // exists for a cancelled transaction, so it cannot be mistaken for success.
    require(kernel.ipc_send(client, client_cap.value(), request.value()));
    require(kernel.retire_ipc_endpoint(server, endpoint.value()));
    auto wake = kernel.threads().wake_reason_of(client);
    require(wake && wake.value() == os::kernel::WakeReason::endpoint_retired);
    require(!kernel.ipc_take_reply(client));
    client_runnable = kernel.runqueue().is_runnable(client);
    require(client_runnable && client_runnable.value());
    require(kernel.live_thread_count() == 2U);

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

    // Inline payloads are a hard ceiling, not a hint. Larger transfer must use
    // the later explicit memory-authority path rather than growing kernel state.
    const std::array<std::byte, os::kernel::max_ipc_inline_bytes + 1U> oversized{};
    require(!os::kernel::IpcEnvelope::from(std::span<const std::byte>{oversized}));

    return 0;
}
