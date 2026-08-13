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
    require(static_cast<bool>(kernel.create_thread(client, 4U)));
    require(static_cast<bool>(kernel.create_thread(server, 2U)));

    auto endpoint = kernel.create_ipc_endpoint(server);
    require(static_cast<bool>(endpoint));

    auto owner_cap = kernel.capabilities().mint(
        server,
        os::kernel::ipc_object_id(endpoint.value()),
        os::kernel::ipc_right_all,
        true);
    require(static_cast<bool>(owner_cap));
    auto client_cap = kernel.capabilities().grant(
        server,
        owner_cap.value(),
        client,
        os::kernel::ipc_right_send,
        false);
    require(static_cast<bool>(client_cap));

    const std::array<std::byte, 4U> request_bytes{
        std::byte{0x43}, std::byte{0x4F}, std::byte{0x4F}, std::byte{0x4B}};
    auto request = os::kernel::IpcEnvelope::from(std::span<const std::byte>{request_bytes});
    require(static_cast<bool>(request));

    require(static_cast<bool>(kernel.ipc_send(client, client_cap.value(), request.value())));
    auto client_runnable = kernel.runqueue().is_runnable(client);
    require(client_runnable && !client_runnable.value());

    auto received = kernel.ipc_receive(server, owner_cap.value());
    require(received && received.value().valid());
    require(received.value().caller == client);
    require(received.value().request == request.value());

    const std::array<std::byte, 2U> reply_bytes{std::byte{0x4F}, std::byte{0x4B}};
    auto response = os::kernel::IpcEnvelope::from(std::span<const std::byte>{reply_bytes});
    require(static_cast<bool>(response));

    require(!kernel.ipc_reply_transaction(
        server, received.value().reply.transaction + 1U, response.value()));
    require(static_cast<bool>(kernel.ipc_reply_transaction(
        server, received.value().reply.transaction, response.value())));
    require(!kernel.ipc_reply_transaction(
        server, received.value().reply.transaction, response.value()));
    client_runnable = kernel.runqueue().is_runnable(client);
    require(client_runnable && client_runnable.value());

    auto collected = kernel.ipc_take_reply(client);
    require(collected && collected.value() == response.value());
    require(!kernel.ipc_take_reply(client));

    require(static_cast<bool>(kernel.ipc_send(client, client_cap.value(), request.value())));
    require(static_cast<bool>(kernel.retire_ipc_endpoint(server, endpoint.value())));
    auto wake = kernel.threads().wake_reason_of(client);
    require(wake && wake.value() == os::kernel::WakeReason::endpoint_retired);
    require(!kernel.ipc_take_reply(client));
    client_runnable = kernel.runqueue().is_runnable(client);
    require(client_runnable && client_runnable.value());
    require(kernel.live_thread_count() == 2U);

    auto endpoint2 = kernel.create_ipc_endpoint(server);
    require(static_cast<bool>(endpoint2));
    auto owner2 = kernel.capabilities().mint(
        server,
        os::kernel::ipc_object_id(endpoint2.value()),
        os::kernel::ipc_right_all,
        true);
    require(static_cast<bool>(owner2));
    auto client2 = kernel.capabilities().grant(
        server,
        owner2.value(),
        client,
        os::kernel::ipc_right_send,
        false);
    require(static_cast<bool>(client2));
    require(static_cast<bool>(kernel.ipc_send(client, client2.value())));

    auto teardown = kernel.destroy_thread(server);
    require(static_cast<bool>(teardown));
    require(teardown.value().ipc_endpoints_retired == 1U);
    require(!kernel.ipc().active(endpoint2.value()));
    wake = kernel.threads().wake_reason_of(client);
    require(wake && wake.value() == os::kernel::WakeReason::endpoint_retired);
    require(kernel.live_thread_count() == 1U);

    {
        os::kernel::Kernel death_kernel{};
        constexpr os::kernel::ThreadId dying_client = 31U;
        constexpr os::kernel::ThreadId live_server = 32U;
        require(static_cast<bool>(death_kernel.create_thread(dying_client, 3U)));
        require(static_cast<bool>(death_kernel.create_thread(live_server, 3U)));
        auto ep = death_kernel.create_ipc_endpoint(live_server);
        require(static_cast<bool>(ep));
        auto owner = death_kernel.capabilities().mint(
            live_server,
            os::kernel::ipc_object_id(ep.value()),
            os::kernel::ipc_right_all,
            true);
        require(static_cast<bool>(owner));
        auto sender = death_kernel.capabilities().grant(
            live_server,
            owner.value(),
            dying_client,
            os::kernel::ipc_right_send,
            false);
        require(static_cast<bool>(sender));
        require(static_cast<bool>(death_kernel.ipc_send(dying_client, sender.value(), request.value())));
        auto in_flight = death_kernel.ipc_receive(live_server, owner.value());
        require(in_flight && in_flight.value().valid());
        require(death_kernel.ipc().pending_call_count() == 1U);
        require(death_kernel.ipc().active_reply_seal_count() == 1U);

        auto dead = death_kernel.destroy_thread(dying_client);
        require(static_cast<bool>(dead));
        require(death_kernel.ipc().pending_call_count() == 0U);
        require(death_kernel.ipc().active_reply_seal_count() == 0U);
        require(death_kernel.live_thread_count() == 1U);
        auto server_state = death_kernel.threads().state_of(live_server);
        require(server_state && server_state.value() == os::kernel::ThreadState::ready);
        require(!death_kernel.ipc_reply_transaction(
            live_server, in_flight.value().reply.transaction));
    }

    // Endpoint restart must settle both scheduler/rendezvous state and the
    // syscall continuation that remembers where a blocked receive should
    // return. Otherwise a restarted endpoint could wake into stale completion.
    {
        os::kernel::Kernel restart_kernel{};
        constexpr os::kernel::ThreadId restart_server = 41U;
        require(static_cast<bool>(restart_kernel.create_thread(restart_server, 3U)));
        auto ep = restart_kernel.create_ipc_endpoint(restart_server);
        require(static_cast<bool>(ep));
        auto owner = restart_kernel.capabilities().mint(
            restart_server,
            os::kernel::ipc_object_id(ep.value()),
            os::kernel::ipc_right_receive,
            false);
        require(static_cast<bool>(owner));

        os::kernel::AddressSpaceEpochAuthority epochs{};
        auto epoch = epochs.acquire();
        require(static_cast<bool>(epoch));
        require(static_cast<bool>(restart_kernel.ipc_arm_receive_continuation(
            restart_server, epoch.value(), owner.value(), 0x7000U, epochs)));
        auto blocked = restart_kernel.ipc_receive(restart_server, owner.value());
        require(blocked && !blocked.value().valid());
        require(restart_kernel.ipc_continuations().receive_armed(restart_server));

        require(static_cast<bool>(restart_kernel.retire_ipc_endpoint(restart_server, ep.value())));
        require(!restart_kernel.ipc_continuations().receive_armed(restart_server));
        auto state = restart_kernel.threads().state_of(restart_server);
        require(state && state.value() == os::kernel::ThreadState::ready);
        auto reason = restart_kernel.threads().wake_reason_of(restart_server);
        require(reason && reason.value() == os::kernel::WakeReason::endpoint_retired);
    }

    const std::array<std::byte, os::kernel::max_ipc_inline_bytes + 1U> oversized{};
    require(!os::kernel::IpcEnvelope::from(std::span<const std::byte>{oversized}));

    return 0;
}
