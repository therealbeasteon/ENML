#include <cstdint>
#include <cstdio>

#include <os/core/error.hpp>
#include <os/kernel/rendezvous.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "rendezvous: %s\n", what);
    }
    return condition;
}

bool refused(const os::core::Result<void>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::kernel &&
        result.error().code == code;
}

bool in_state(
    const os::kernel::Rendezvous& kernel,
    os::kernel::ThreadId thread,
    os::kernel::ThreadState expected) {
    auto state = kernel.state_of(thread);
    return state && state.value() == expected;
}

constexpr os::kernel::ThreadId client = 10U;
constexpr os::kernel::ThreadId server = 20U;
constexpr os::kernel::ThreadId bystander = 30U;

} // namespace

int main() {
    {
        os::kernel::Rendezvous kernel;
        if (!check(static_cast<bool>(kernel.create_thread(client)) &&
                   static_cast<bool>(kernel.create_thread(server)),
                   "thread creation failed")) return 1;
        if (!check(kernel.live_thread_count() == 2U, "wrong live count")) return 1;

        if (!check(static_cast<bool>(kernel.send(client, server)), "send refused")) return 1;
        if (!check(in_state(kernel, client, os::kernel::ThreadState::send_blocked),
                   "sender not waiting to be collected")) return 1;

        auto received = kernel.receive(server);
        if (!check(static_cast<bool>(received) && received.value() == client,
                   "receive did not return the sender")) return 1;
        if (!check(in_state(kernel, client, os::kernel::ThreadState::reply_blocked),
                   "sender not awaiting a reply")) return 1;
        if (!check(in_state(kernel, server, os::kernel::ThreadState::ready),
                   "server not runnable after collecting")) return 1;

        if (!check(static_cast<bool>(kernel.reply(server, client)), "reply refused")) return 1;
        if (!check(in_state(kernel, client, os::kernel::ThreadState::ready),
                   "reply did not wake the caller")) return 1;
        auto wake = kernel.wake_reason_of(client);
        if (!check(wake && wake.value() == os::kernel::WakeReason::replied,
                   "caller could not tell it was answered")) return 1;
    }

    // Receiver first: preserve the caller identity across the blocked receive.
    // A real syscall resumes after the later send and still needs to know which
    // caller it is authorized to reply to; dropping that identity would force an
    // unsafe side channel or a second kernel queue.
    {
        os::kernel::Rendezvous kernel;
        (void)kernel.create_thread(client);
        (void)kernel.create_thread(server);

        auto empty = kernel.receive(server);
        if (!check(static_cast<bool>(empty) && empty.value() == os::kernel::invalid_thread,
                   "receive with nothing pending did not park")) return 1;
        if (!check(in_state(kernel, server, os::kernel::ThreadState::receive_blocked),
                   "server not parked")) return 1;

        if (!check(static_cast<bool>(kernel.send(client, server)), "send refused")) return 1;
        if (!check(in_state(kernel, client, os::kernel::ThreadState::reply_blocked),
                   "sender should have gone straight to awaiting a reply")) return 1;
        if (!check(in_state(kernel, server, os::kernel::ThreadState::ready),
                   "waiting server not woken")) return 1;

        auto delivered = kernel.receive(server);
        if (!check(delivered && delivered.value() == client,
                   "woken receive lost delivered caller identity")) return 1;
        auto partner = kernel.partner_of(server);
        if (!check(partner && partner.value() == os::kernel::invalid_thread,
                   "delivered caller identity was not consumed exactly once")) return 1;
    }

    {
        os::kernel::Rendezvous kernel;
        (void)kernel.create_thread(client);
        (void)kernel.create_thread(server);
        (void)kernel.create_thread(bystander);

        (void)kernel.send(client, server);
        (void)kernel.receive(server);
        if (!check(in_state(kernel, client, os::kernel::ThreadState::reply_blocked),
                   "setup failed")) return 1;

        if (!check(refused(kernel.reply(bystander, client),
                           os::kernel::rendezvous_errors::not_awaiting_reply),
                   "an unrelated thread answered someone else's caller")) return 1;
        if (!check(in_state(kernel, client, os::kernel::ThreadState::reply_blocked),
                   "refused reply changed the caller's state")) return 1;

        if (!check(refused(kernel.reply(server, bystander),
                           os::kernel::rendezvous_errors::not_awaiting_reply),
                   "server answered a thread that had not called")) return 1;

        if (!check(static_cast<bool>(kernel.reply(server, client)), "reply refused")) return 1;
        if (!check(refused(kernel.reply(server, client),
                           os::kernel::rendezvous_errors::not_awaiting_reply),
                   "caller answered twice")) return 1;
    }

    {
        os::kernel::Rendezvous kernel;
        (void)kernel.create_thread(client);
        (void)kernel.create_thread(server);
        (void)kernel.create_thread(bystander);

        (void)kernel.send(client, server);
        (void)kernel.receive(server);
        (void)kernel.send(bystander, server);
        if (!check(in_state(kernel, bystander, os::kernel::ThreadState::send_blocked),
                   "second sender not waiting")) return 1;

        auto released = kernel.exit_thread(server);
        if (!check(static_cast<bool>(released), "exit failed")) return 1;
        if (!check(released.value() == 2U, "exit did not release both waiters")) return 1;
        if (!check(in_state(kernel, client, os::kernel::ThreadState::ready),
                   "caller left blocked on a dead server")) return 1;
        if (!check(in_state(kernel, bystander, os::kernel::ThreadState::ready),
                   "sender left blocked on a dead server")) return 1;

        for (const auto thread : {client, bystander}) {
            auto wake = kernel.wake_reason_of(thread);
            if (!check(wake && wake.value() == os::kernel::WakeReason::peer_exited,
                       "released thread thought it had been answered")) return 1;
        }

        if (!check(!kernel.state_of(server), "exited thread still resolvable")) return 1;
        if (!check(kernel.live_thread_count() == 2U, "live count wrong after exit")) return 1;
    }

    {
        os::kernel::Rendezvous kernel;
        (void)kernel.create_thread(client);
        (void)kernel.create_thread(server);

        if (!check(refused(kernel.send(client, client),
                           os::kernel::rendezvous_errors::self_addressed),
                   "self-addressed send accepted")) return 1;
        if (!check(refused(kernel.send(client, os::kernel::invalid_thread),
                           os::kernel::rendezvous_errors::invalid_thread_id),
                   "send to the invalid thread accepted")) return 1;
        if (!check(refused(kernel.send(client, 999U),
                           os::kernel::rendezvous_errors::unknown_thread),
                   "send to an unknown thread accepted")) return 1;

        (void)kernel.send(client, server);
        if (!check(refused(kernel.send(client, server),
                           os::kernel::rendezvous_errors::not_runnable),
                   "blocked thread sent again")) return 1;
        if (!check(!kernel.receive(client), "blocked thread received")) return 1;
    }

    {
        os::kernel::Rendezvous kernel;
        if (!check(static_cast<bool>(kernel.create_thread(client)), "creation failed")) return 1;
        if (!check(refused(kernel.create_thread(client),
                           os::kernel::rendezvous_errors::thread_exists),
                   "duplicate thread accepted")) return 1;
        if (!check(refused(kernel.create_thread(os::kernel::invalid_thread),
                           os::kernel::rendezvous_errors::invalid_thread_id),
                   "the invalid thread id was created")) return 1;

        for (std::uint32_t extra = 2U; extra <= os::kernel::max_threads; ++extra) {
            if (!check(static_cast<bool>(kernel.create_thread(1000U + extra)),
                       "creation within the ceiling refused")) return 1;
        }
        if (!check(kernel.live_thread_count() == os::kernel::max_threads,
                   "wrong count at the ceiling")) return 1;
        if (!check(refused(kernel.create_thread(50'000U),
                           os::kernel::rendezvous_errors::thread_limit),
                   "creation past the ceiling accepted")) return 1;
    }

    {
        os::kernel::Rendezvous kernel;
        constexpr os::kernel::Priority low = 1U;
        constexpr os::kernel::Priority high = 9U;

        if (!check(static_cast<bool>(kernel.create_thread(server, low)) &&
                   static_cast<bool>(kernel.create_thread(client, low)) &&
                   static_cast<bool>(kernel.create_thread(bystander, high)),
                   "creation with priorities failed")) return 1;

        auto effective = kernel.effective_priority_of(server);
        if (!check(effective && effective.value() == low,
                   "idle server did not run at its own priority")) return 1;

        (void)kernel.send(client, server);
        (void)kernel.receive(server);
        effective = kernel.effective_priority_of(server);
        if (!check(effective && effective.value() == low,
                   "low-priority caller raised the server")) return 1;

        (void)kernel.send(bystander, server);
        effective = kernel.effective_priority_of(server);
        if (!check(effective && effective.value() == high,
                   "server did not inherit the waiting thread's priority")) return 1;
        auto base = kernel.base_priority_of(server);
        if (!check(base && base.value() == low, "inheritance overwrote the base priority")) return 1;

        (void)kernel.reply(server, client);
        auto collected = kernel.receive(server);
        if (!check(static_cast<bool>(collected) && collected.value() == bystander,
                   "server did not collect the waiting high-priority caller")) return 1;
        effective = kernel.effective_priority_of(server);
        if (!check(effective && effective.value() == high,
                   "server dropped priority while still serving the caller")) return 1;

        (void)kernel.reply(server, bystander);
        effective = kernel.effective_priority_of(server);
        if (!check(effective && effective.value() == low,
                   "server stayed elevated after its callers were answered")) return 1;

        (void)kernel.send(bystander, server);
        effective = kernel.effective_priority_of(server);
        if (!check(effective && effective.value() == high, "donation not applied")) return 1;
        (void)kernel.exit_thread(bystander);
        effective = kernel.effective_priority_of(server);
        if (!check(effective && effective.value() == low,
                   "a dead caller kept donating its priority")) return 1;
    }

    return 0;
}
