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
    // A full rendezvous, sender first: the sender waits to be collected, the
    // server takes the message, the sender is then awaiting an answer.
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

    // Receiver first: the server parks, and a later send completes the
    // rendezvous immediately rather than parking the sender too.
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
    }

    // The authority rule. Reply is bound to a received message, not to a thread
    // id taken from a register: a thread may only answer a caller that is
    // awaiting *its* reply.
    {
        os::kernel::Rendezvous kernel;
        (void)kernel.create_thread(client);
        (void)kernel.create_thread(server);
        (void)kernel.create_thread(bystander);

        (void)kernel.send(client, server);
        (void)kernel.receive(server);
        if (!check(in_state(kernel, client, os::kernel::ThreadState::reply_blocked),
                   "setup failed")) return 1;

        // A thread that is not part of the conversation cannot end it.
        if (!check(refused(kernel.reply(bystander, client),
                           os::kernel::rendezvous_errors::not_awaiting_reply),
                   "an unrelated thread answered someone else's caller")) return 1;
        if (!check(in_state(kernel, client, os::kernel::ThreadState::reply_blocked),
                   "refused reply changed the caller's state")) return 1;

        // Nor can a server answer a thread that never called it.
        if (!check(refused(kernel.reply(server, bystander),
                           os::kernel::rendezvous_errors::not_awaiting_reply),
                   "server answered a thread that had not called")) return 1;

        // Answering twice is refused: after the first reply the caller is no
        // longer awaiting one.
        if (!check(static_cast<bool>(kernel.reply(server, client)), "reply refused")) return 1;
        if (!check(refused(kernel.reply(server, client),
                           os::kernel::rendezvous_errors::not_awaiting_reply),
                   "caller answered twice")) return 1;
    }

    // Exit releases everyone waiting, and tells them it was a death rather than
    // an answer. A crashing server must not park its clients forever.
    {
        os::kernel::Rendezvous kernel;
        (void)kernel.create_thread(client);
        (void)kernel.create_thread(server);
        (void)kernel.create_thread(bystander);

        // One caller collected and awaiting a reply, one still waiting to be
        // collected. Both are blocked on the server and both must be freed.
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

        // Told it was a death, not an answer. A caller that cannot tell them
        // apart will treat the death as a reply.
        for (const auto thread : {client, bystander}) {
            auto wake = kernel.wake_reason_of(thread);
            if (!check(wake && wake.value() == os::kernel::WakeReason::peer_exited,
                       "released thread thought it had been answered")) return 1;
        }

        // The dead thread is gone, and a stale reference resolves to a definite
        // answer rather than to whoever is created next.
        if (!check(!kernel.state_of(server), "exited thread still resolvable")) return 1;
        if (!check(kernel.live_thread_count() == 2U, "live count wrong after exit")) return 1;
    }

    // Transitions are checked against the current state, never assumed.
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
        // Already blocked: it cannot send or receive again.
        if (!check(refused(kernel.send(client, server),
                           os::kernel::rendezvous_errors::not_runnable),
                   "blocked thread sent again")) return 1;
        if (!check(!kernel.receive(client), "blocked thread received")) return 1;
    }

    // Duplicate creation is refused, and so is exceeding the ceiling - the
    // fixed bound is what lets every structure here be an array with no
    // allocator underneath it.
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

    // Priority inheritance. A server runs at the priority of whoever is
    // waiting for it, so a low-priority thread cannot park a shared server and
    // stall the high-priority threads that need it.
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

        // A low-priority client parks the server's work. On its own this does
        // not raise the server.
        (void)kernel.send(client, server);
        (void)kernel.receive(server);
        effective = kernel.effective_priority_of(server);
        if (!check(effective && effective.value() == low,
                   "low-priority caller raised the server")) return 1;

        // A high-priority thread now needs the same server. Without
        // inheritance it waits behind work running at low priority, which is
        // the inversion: it is stalled by a thread it outranks.
        (void)kernel.send(bystander, server);
        effective = kernel.effective_priority_of(server);
        if (!check(effective && effective.value() == high,
                   "server did not inherit the waiting thread's priority")) return 1;
        auto base = kernel.base_priority_of(server);
        if (!check(base && base.value() == low, "inheritance overwrote the base priority")) return 1;

        // The donation ends when the waiting ends, and it is recomputed rather
        // than decremented - a server left permanently high is as much a defect
        // as one left too low.
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

        // A caller that dies while waiting stops donating, or a crashed
        // high-priority client would pin a server forever.
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
