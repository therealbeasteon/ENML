#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <os/core/error.hpp>
#include <os/kernel/kernel.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "kernel: %s\n", what);
    }
    return condition;
}

constexpr os::kernel::ThreadId alice = 10U;
constexpr os::kernel::ThreadId bob = 20U;
constexpr os::kernel::ThreadId carol = 30U;

constexpr os::kernel::ObjectId object = 0x900U;
constexpr os::kernel::InterruptSource line = 4U;

} // namespace

int main() {
    // Creating a thread reaches both tables, and the scheduler will actually
    // pick it. A thread the rendezvous knows about and the scheduler does not is
    // one that can be sent to and will never run.
    {
        os::kernel::Kernel kernel;
        if (!check(static_cast<bool>(kernel.create_thread(alice)), "create refused")) return 1;
        if (!check(kernel.live_thread_count() == 1U, "wrong live count")) return 1;

        const auto decision = kernel.schedule(0U);
        if (!check(decision.thread == alice, "a created thread was not schedulable")) return 1;
    }

    // The headline obligation: a thread's death reaches every table. Blocked
    // peers released, capabilities surrendered along with everything derived
    // from them, interrupt sources released masked, and off the run queue.
    //
    // Each of those is already correct on its own. The failure this guards is a
    // forgotten call, which fails silently and leaves authority behind.
    {
        os::kernel::Kernel kernel;
        (void)kernel.create_thread(alice);
        (void)kernel.create_thread(bob);
        (void)kernel.create_thread(carol);

        // Alice holds authority and passes some of it on.
        auto root = kernel.capabilities().mint(alice, object, 0b11U, true);
        if (!check(static_cast<bool>(root), "mint refused")) return 1;
        auto derived = kernel.capabilities().grant(alice, root.value(), bob, 0b01U, false);
        if (!check(static_cast<bool>(derived), "grant refused")) return 1;

        // Alice drives a device.
        if (!check(static_cast<bool>(kernel.interrupts().attach(alice, line)),
                   "attach refused")) return 1;

        // Carol is waiting on Alice.
        if (!check(static_cast<bool>(kernel.send(carol, alice)), "send refused")) return 1;

        const auto teardown = kernel.destroy_thread(alice);
        if (!check(static_cast<bool>(teardown), "destroy refused")) return 1;
        if (!check(teardown.value().threads_released == 1U,
                   "the thread blocked on the dead one was not released")) return 1;
        if (!check(teardown.value().capabilities_revoked == 2U,
                   "the dead thread's authority outlived it")) return 1;
        if (!check(teardown.value().interrupt_sources_released == 1U,
                   "the dead thread's interrupt source outlived it")) return 1;

        // And the tables agree with the report.
        if (!check(!static_cast<bool>(kernel.capabilities().describe(root.value())) &&
                       !static_cast<bool>(kernel.capabilities().describe(derived.value())),
                   "a revoked capability still resolves")) return 1;
        if (!check(!static_cast<bool>(kernel.interrupts().state_of(line)),
                   "a released interrupt source is still attached")) return 1;
        if (!check(kernel.live_thread_count() == 2U, "wrong live count after death")) return 1;

        // Carol was released, so she is runnable - which the scheduler has to
        // have been told, or she waits forever on a thread that no longer exists.
        const auto decision = kernel.schedule(0U);
        if (!check(decision.thread == carol || decision.thread == bob,
                   "nobody was schedulable after the death")) return 1;
        const auto carol_state = kernel.threads().state_of(carol);
        if (!check(carol_state && carol_state.value() == os::kernel::ThreadState::ready,
                   "the released thread is not ready")) return 1;
    }

    // Priority inheritance has to reach the scheduler, not merely exist in the
    // rendezvous. A low-priority server serving a high-priority client must beat
    // an unrelated middle-priority thread - which is the whole point of M7.1b,
    // and is only true if the composition pushes the inherited value across.
    {
        os::kernel::Kernel kernel;
        constexpr os::kernel::ThreadId server = alice;
        constexpr os::kernel::ThreadId client = bob;
        constexpr os::kernel::ThreadId bystander = carol;

        (void)kernel.create_thread(server, 0U);
        (void)kernel.create_thread(client, 9U);
        (void)kernel.create_thread(bystander, 5U);

        // The server parks waiting for work.
        auto parked = kernel.receive(server);
        if (!check(static_cast<bool>(parked), "receive refused")) return 1;

        // Nothing is inherited yet, so the most urgent *runnable* thread runs -
        // which is the client, still going about its own business.
        if (!check(kernel.schedule(0U).thread == client,
                   "the most urgent runnable thread was not chosen")) return 1;

        // The high-priority client calls the server.
        if (!check(static_cast<bool>(kernel.send(client, server)), "send refused")) return 1;

        // Now the server runs at the client's priority and takes the processor.
        // Its own priority is zero; without inheritance reaching the scheduler
        // the bystander would still be chosen.
        if (!check(kernel.schedule(1000U).thread == server,
                   "inherited priority did not reach the scheduler")) return 1;

        // On reply the donation stops and the bystander gets it back.
        if (!check(static_cast<bool>(kernel.reply(server, client)), "reply refused")) return 1;
        if (!check(kernel.schedule(2000U).thread == client,
                   "the answered client did not become the most urgent")) return 1;
    }

    // An interrupt makes its driver runnable. The interrupt table knows a driver
    // should run and the scheduler decides who does; a dispatch that marks a
    // source pending without making its driver runnable is never serviced.
    {
        os::kernel::Kernel kernel;
        (void)kernel.create_thread(alice, 1U);
        (void)kernel.create_thread(bob, 0U);
        (void)kernel.interrupts().attach(bob, line);

        // Bob parks; Alice is the only runnable thread.
        (void)kernel.receive(bob);
        if (!check(kernel.schedule(0U).thread == alice, "the parked driver was scheduled")) {
            return 1;
        }

        // The device asserts. Bob is still receive-blocked in the rendezvous, so
        // the kernel must not pretend otherwise - the interrupt is recorded and
        // the driver is woken by the transport that actually delivers it.
        auto taken = kernel.dispatch_interrupt(line);
        if (!check(static_cast<bool>(taken), "dispatch refused")) return 1;
        if (!check(taken.value().owner == bob, "dispatch named the wrong owner")) return 1;
        if (!check(taken.value().wake, "the first assertion did not ask for a wakeup")) return 1;

        auto masked = kernel.interrupts().is_masked(line);
        if (!check(masked && masked.value(), "the source was not masked on dispatch")) return 1;
    }

    // Yield goes through the scheduler and forfeits the remainder, and a thread
    // the kernel does not know cannot yield.
    {
        os::kernel::Kernel kernel;
        (void)kernel.create_thread(alice);
        (void)kernel.create_thread(bob);
        (void)kernel.schedule(0U);

        if (!check(static_cast<bool>(kernel.yield(alice)), "yield refused")) return 1;
        if (!check(kernel.schedule(1000U).thread == bob, "yield did not give up the turn")) {
            return 1;
        }
        if (!check(!static_cast<bool>(kernel.yield(999U)),
                   "an unknown thread was allowed to yield")) return 1;
    }

    // Destroying a thread twice is refused rather than double-counted, and a
    // thread that never existed cannot be destroyed.
    {
        os::kernel::Kernel kernel;
        (void)kernel.create_thread(alice);
        if (!check(static_cast<bool>(kernel.destroy_thread(alice)), "destroy refused")) return 1;
        if (!check(!static_cast<bool>(kernel.destroy_thread(alice)),
                   "a thread was destroyed twice")) return 1;
        if (!check(!static_cast<bool>(kernel.destroy_thread(bob)),
                   "an unknown thread was destroyed")) return 1;
        if (!check(kernel.live_thread_count() == 0U, "wrong live count")) return 1;
    }

    // The ceiling holds through the composition, and slots are reusable after a
    // death - which is what makes a restarted service a recovery rather than a
    // reboot.
    {
        os::kernel::Kernel kernel;
        for (std::size_t i = 0U; i < os::kernel::max_threads; ++i) {
            const auto thread = static_cast<os::kernel::ThreadId>(i + 1U);
            if (!check(static_cast<bool>(kernel.create_thread(thread)),
                       "create refused below the ceiling")) return 1;
        }
        const auto beyond = static_cast<os::kernel::ThreadId>(os::kernel::max_threads + 1U);
        if (!check(!static_cast<bool>(kernel.create_thread(beyond)),
                   "the thread table grew past its ceiling")) return 1;

        if (!check(static_cast<bool>(kernel.destroy_thread(1U)), "destroy refused")) return 1;
        if (!check(static_cast<bool>(kernel.create_thread(beyond)),
                   "a slot was not reusable after a death")) return 1;
    }

    return 0;
}
