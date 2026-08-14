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

    // M7.9: interrupt_attach/detach/complete are capability-checked at this
    // composition layer, since InterruptTable itself never sees a capability.
    // A driver with the right capability may attach, service and detach; the
    // wrong object, a missing right, or a capability revoked mid-flight are
    // each refused before InterruptTable is ever consulted.
    {
        os::kernel::Kernel kernel;
        (void)kernel.create_thread(alice);
        (void)kernel.create_thread(bob);

        auto right_cap = kernel.capabilities().mint(
            alice, os::kernel::interrupt_object_id(line),
            os::kernel::interrupt_right_attach, false);
        if (!check(static_cast<bool>(right_cap), "mint refused")) return 1;

        // Wrong object: a capability that names some other object entirely.
        auto wrong_object_cap = kernel.capabilities().mint(alice, object, 0xFFU, false);
        if (!check(static_cast<bool>(wrong_object_cap), "mint refused")) return 1;
        if (!check(!static_cast<bool>(kernel.interrupt_attach(alice, wrong_object_cap.value())),
                   "a capability naming the wrong object attached")) return 1;

        // Missing right: a capability over the right object, but no attach right.
        auto no_rights_cap = kernel.capabilities().mint(
            alice, os::kernel::interrupt_object_id(line), 0U, false);
        if (!check(static_cast<bool>(no_rights_cap), "mint refused")) return 1;
        if (!check(!static_cast<bool>(kernel.interrupt_attach(alice, no_rights_cap.value())),
                   "a capability with no rights attached")) return 1;

        // Someone else's capability: bob cannot exercise alice's.
        if (!check(!static_cast<bool>(kernel.interrupt_attach(bob, right_cap.value())),
                   "a non-holder attached through another thread's capability")) return 1;

        // The correct capability attaches, and the source is really owned.
        if (!check(static_cast<bool>(kernel.interrupt_attach(alice, right_cap.value())),
                   "a correct capability was refused")) return 1;
        auto owner = kernel.interrupts().owner_of(line);
        if (!check(owner && owner.value() == alice, "the source has the wrong owner")) return 1;

        // A second attach through the same capability is refused - the source
        // is already attached, and InterruptTable's own state machine (not
        // this layer) is what says so.
        if (!check(!static_cast<bool>(kernel.interrupt_attach(alice, right_cap.value())),
                   "a source was attached twice")) return 1;

        // Complete with nothing outstanding is refused by InterruptTable, not
        // silently accepted by the capability layer above it.
        if (!check(!static_cast<bool>(kernel.interrupt_complete(alice, right_cap.value())),
                   "completed a source with nothing outstanding")) return 1;

        // A capability revoked mid-flight must not still authorize detach -
        // no caching, the same rule IPC's endpoint_for_capability keeps.
        (void)kernel.capabilities().revoke(alice, right_cap.value());
        if (!check(!static_cast<bool>(kernel.interrupt_detach(alice, right_cap.value())),
                   "a revoked capability still authorized detach")) return 1;
        owner = kernel.interrupts().owner_of(line);
        if (!check(owner && owner.value() == alice,
                   "the source was released without a valid capability")) return 1;

        // Only thread death releases it now - detach_all_owned_by, exercised
        // through destroy_thread, is the fail-closed path for exactly this.
        const auto teardown = kernel.destroy_thread(alice);
        if (!check(static_cast<bool>(teardown), "destroy refused")) return 1;
        if (!check(teardown.value().interrupt_sources_released == 1U,
                   "the orphaned source was not released on death")) return 1;

        // A fresh capability over the same source lets a new driver attach.
        auto fresh_cap = kernel.capabilities().mint(
            bob, os::kernel::interrupt_object_id(line),
            os::kernel::interrupt_right_attach, false);
        if (!check(static_cast<bool>(fresh_cap), "mint refused")) return 1;
        if (!check(static_cast<bool>(kernel.interrupt_attach(bob, fresh_cap.value())),
                   "a fresh capability could not attach after release")) return 1;
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
