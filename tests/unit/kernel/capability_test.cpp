#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <os/core/error.hpp>
#include <os/kernel/capability.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "capability: %s\n", what);
    }
    return condition;
}

template <typename T>
bool refused(const os::core::Result<T>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::kernel &&
        result.error().code == code;
}

// Threads are plain identifiers here. The capability table deliberately does not
// consult the rendezvous: two state machines that each know about the other are
// two state machines neither of which can be tested alone. Composing them - a
// thread that exits surrenders what it held - is the job of the layer above.
constexpr os::kernel::ThreadId broker = 1U;
constexpr os::kernel::ThreadId alice = 10U;
constexpr os::kernel::ThreadId bob = 20U;
constexpr os::kernel::ThreadId carol = 30U;
constexpr os::kernel::ThreadId mallory = 40U;

constexpr os::kernel::ObjectId object = 0x5000U;
constexpr os::kernel::Rights read_write = 0b11U;
constexpr os::kernel::Rights read_only = 0b01U;

bool live(const os::kernel::CapabilityTable& table, os::kernel::CapabilityId capability) {
    return static_cast<bool>(table.describe(capability));
}

// Every live capability's parent must still be live. A revoke that removed a
// parent while leaving a child behind would produce authority nobody can take
// back, which is the failure this whole design exists to avoid.
bool no_orphans(
    const os::kernel::CapabilityTable& table,
    const os::kernel::CapabilityId* ids,
    std::size_t count) {
    for (std::size_t i = 0U; i < count; ++i) {
        auto described = table.describe(ids[i]);
        if (!described) continue;
        const auto parent = described.value().parent;
        if (parent == os::kernel::invalid_capability) continue;
        if (!live(table, parent)) return false;
    }
    return true;
}

} // namespace

int main() {
    // A minted root is authority that did not previously exist: no parent, depth
    // zero, held by exactly the thread it was minted for.
    {
        os::kernel::CapabilityTable table;
        auto root = table.mint(alice, object, read_write, true);
        if (!check(static_cast<bool>(root), "mint refused")) return 1;
        if (!check(table.live_capability_count() == 1U, "wrong live count")) return 1;

        auto described = table.describe(root.value());
        if (!check(static_cast<bool>(described), "describe refused")) return 1;
        const auto info = described.value();
        if (!check(info.parent == os::kernel::invalid_capability, "root has a parent")) return 1;
        if (!check(static_cast<std::size_t>(info.depth) == 0U,
                   "root is not at depth zero")) return 1;
        if (!check(info.holder == alice, "wrong holder")) return 1;
        if (!check(info.object == object, "wrong object")) return 1;
        if (!check(info.rights == read_write, "wrong rights")) return 1;

        if (!check(table.holds(alice, root.value()), "holder does not hold it")) return 1;
        if (!check(!table.holds(bob, root.value()), "a non-holder appears to hold it")) return 1;
    }

    // Authority held by nobody, or over nothing, is refused rather than stored.
    {
        os::kernel::CapabilityTable table;
        if (!check(refused(table.mint(os::kernel::invalid_thread, object, read_write, true),
                           os::kernel::capability_errors::invalid_holder),
                   "minted a capability nobody holds")) return 1;
        if (!check(refused(table.mint(alice, os::kernel::invalid_object, read_write, true),
                           os::kernel::capability_errors::invalid_object_id),
                   "minted a capability over nothing")) return 1;
        if (!check(table.live_capability_count() == 0U, "refused mints left state")) return 1;
    }

    // Attenuation only attenuates. A subset of the parent's rights is granted;
    // anything outside them is an escalation and is refused.
    {
        os::kernel::CapabilityTable table;
        auto root = table.mint(alice, object, read_write, true);
        if (!check(static_cast<bool>(root), "mint refused")) return 1;

        auto weaker = table.grant(alice, root.value(), bob, read_only, false);
        if (!check(static_cast<bool>(weaker), "attenuating grant refused")) return 1;

        auto info = table.describe(weaker.value());
        if (!check(static_cast<bool>(info), "describe of the child refused")) return 1;
        if (!check(info.value().rights == read_only, "child did not attenuate")) return 1;
        if (!check(info.value().parent == root.value(), "child does not record its parent")) return 1;
        if (!check(static_cast<std::size_t>(info.value().depth) == 1U,
                   "child is at the wrong depth")) return 1;
        if (!check(info.value().object == object,
                   "child names a different object from its parent")) return 1;

        if (!check(refused(table.grant(alice, root.value(), carol, os::kernel::all_rights, false),
                           os::kernel::capability_errors::rights_escalation),
                   "a grant added rights the parent did not have")) return 1;
    }

    // Holding it is the authority to pass it on, and nothing else is. A thread
    // that does not hold a capability cannot grant it, however well it guesses.
    {
        os::kernel::CapabilityTable table;
        auto root = table.mint(alice, object, read_write, true);
        if (!check(static_cast<bool>(root), "mint refused")) return 1;

        if (!check(refused(table.grant(mallory, root.value(), carol, read_only, false),
                           os::kernel::capability_errors::not_holder),
                   "a non-holder granted a capability")) return 1;
        if (!check(refused(table.grant(alice, root.value(), alice, read_only, false),
                           os::kernel::capability_errors::self_addressed),
                   "a thread granted to itself")) return 1;
        if (!check(refused(table.grant(alice, 9999U, carol, read_only, false),
                           os::kernel::capability_errors::unknown_capability),
                   "granted a capability that does not exist")) return 1;
    }

    // The answer to unbounded proliferation: a capability granted without the
    // right to pass it on is a leaf, and its holder cannot make it anything else.
    {
        os::kernel::CapabilityTable table;
        auto root = table.mint(alice, object, read_write, true);
        auto leaf = table.grant(alice, root.value(), bob, read_write, false);
        if (!check(static_cast<bool>(leaf), "grant refused")) return 1;

        if (!check(refused(table.grant(bob, leaf.value(), carol, read_write, false),
                           os::kernel::capability_errors::not_transferable),
                   "a non-transferable capability was passed on")) return 1;
        if (!check(refused(table.grant(bob, leaf.value(), carol, read_write, true),
                           os::kernel::capability_errors::not_transferable),
                   "a non-transferable capability was passed on as transferable")) return 1;
    }

    // The delegation chain has a stated ceiling, and it is the ceiling that
    // makes revocation bounded rather than a function of how much delegation an
    // attacker arranged first.
    {
        os::kernel::CapabilityTable table;
        auto current = table.mint(alice, object, read_write, true);
        if (!check(static_cast<bool>(current), "mint refused")) return 1;

        os::kernel::CapabilityId held = current.value();
        os::kernel::ThreadId holder = alice;
        for (std::size_t step = 0U; step < os::kernel::max_derivation_depth; ++step) {
            const os::kernel::ThreadId next = static_cast<os::kernel::ThreadId>(100U + step);
            auto granted = table.grant(holder, held, next, read_write, true);
            if (!check(static_cast<bool>(granted), "a grant within the depth ceiling was refused")) {
                return 1;
            }
            held = granted.value();
            holder = next;
        }

        auto described = table.describe(held);
        if (!check(static_cast<bool>(described) &&
                       static_cast<std::size_t>(described.value().depth) ==
                           os::kernel::max_derivation_depth,
                   "the chain did not reach the ceiling")) return 1;

        const os::kernel::ThreadId beyond = 999U;
        if (!check(refused(table.grant(holder, held, beyond, read_write, true),
                           os::kernel::capability_errors::derivation_too_deep),
                   "the delegation chain grew past its ceiling")) return 1;
    }

    // Selective revocation - the property the references record as absent from
    // capability systems generally. Alice grants to Bob and to Carol; taking
    // back Bob's leaves Carol's and Alice's own untouched.
    {
        os::kernel::CapabilityTable table;
        auto root = table.mint(alice, object, read_write, true);
        auto to_bob = table.grant(alice, root.value(), bob, read_write, true);
        auto to_carol = table.grant(alice, root.value(), carol, read_write, true);
        if (!check(static_cast<bool>(to_bob) && static_cast<bool>(to_carol),
                   "sibling grants refused")) return 1;
        if (!check(table.live_capability_count() == 3U, "wrong live count")) return 1;

        auto removed = table.revoke(alice, to_bob.value());
        if (!check(static_cast<bool>(removed) && removed.value() == 1U,
                   "revoking one grant did not remove exactly one capability")) return 1;
        if (!check(!live(table, to_bob.value()), "the revoked capability survived")) return 1;
        if (!check(live(table, to_carol.value()), "a sibling was revoked too")) return 1;
        if (!check(live(table, root.value()), "the parent was revoked too")) return 1;
        if (!check(table.live_capability_count() == 2U, "wrong live count after revoke")) return 1;
    }

    // Revoking takes the whole derivation subtree, because authority that was
    // passed on is still authority that came from here.
    {
        os::kernel::CapabilityTable table;
        auto root = table.mint(alice, object, read_write, true);
        auto second = table.grant(alice, root.value(), bob, read_write, true);
        auto third = table.grant(bob, second.value(), carol, read_write, true);
        auto fourth = table.grant(carol, third.value(), mallory, read_write, true);
        if (!check(static_cast<bool>(fourth), "chain grants refused")) return 1;
        if (!check(table.live_capability_count() == 4U, "wrong live count")) return 1;

        const os::kernel::CapabilityId ids[] = {
            root.value(), second.value(), third.value(), fourth.value()};

        // Alice took back what she gave Bob; everything downstream of it goes
        // with it, and Alice's own root does not.
        auto removed = table.revoke(alice, second.value());
        if (!check(static_cast<bool>(removed) && removed.value() == 3U,
                   "revoking a subtree did not remove all of it")) return 1;
        if (!check(live(table, root.value()), "the root was removed")) return 1;
        if (!check(!live(table, second.value()) && !live(table, third.value()) &&
                       !live(table, fourth.value()),
                   "part of the subtree survived")) return 1;
        if (!check(no_orphans(table, ids, 4U), "revocation left an orphan")) return 1;
        if (!check(table.live_capability_count() == 1U, "wrong live count after revoke")) return 1;
    }

    // Who may revoke, and who may not. The holder and the holder of the parent;
    // a sibling's holder has no say, and neither does a stranger.
    {
        os::kernel::CapabilityTable table;
        auto root = table.mint(alice, object, read_write, true);
        auto to_bob = table.grant(alice, root.value(), bob, read_write, true);
        auto to_carol = table.grant(alice, root.value(), carol, read_write, true);
        if (!check(static_cast<bool>(to_bob) && static_cast<bool>(to_carol),
                   "grants refused")) return 1;

        if (!check(refused(table.revoke(carol, to_bob.value()),
                           os::kernel::capability_errors::not_revocable),
                   "a sibling holder revoked someone else's capability")) return 1;
        if (!check(refused(table.revoke(mallory, to_bob.value()),
                           os::kernel::capability_errors::not_revocable),
                   "a stranger revoked a capability")) return 1;
        if (!check(refused(table.revoke(bob, root.value()),
                           os::kernel::capability_errors::not_revocable),
                   "a child's holder revoked its parent")) return 1;
        if (!check(live(table, to_bob.value()), "a refused revoke removed something")) return 1;

        // The holder may always surrender its own.
        auto dropped = table.revoke(bob, to_bob.value());
        if (!check(static_cast<bool>(dropped) && dropped.value() == 1U,
                   "a holder could not surrender its own capability")) return 1;
    }

    // Identifiers are never reused, so a stale reference resolves to a definite
    // answer rather than to whatever authority was minted next.
    {
        os::kernel::CapabilityTable table;
        auto first = table.mint(alice, object, read_write, true);
        if (!check(static_cast<bool>(first), "mint refused")) return 1;
        auto dropped = table.revoke(alice, first.value());
        if (!check(static_cast<bool>(dropped), "revoke refused")) return 1;

        auto second = table.mint(bob, object, read_write, true);
        if (!check(static_cast<bool>(second), "mint after revoke refused")) return 1;
        if (!check(second.value() != first.value(), "a capability id was reused")) return 1;
        if (!check(refused(table.describe(first.value()),
                           os::kernel::capability_errors::unknown_capability),
                   "a retired capability id still resolves")) return 1;
    }

    // A dead thread holds nothing. Everything it held goes, and so does
    // everything derived from what it held - including capabilities held by
    // threads that are still alive, because that authority only ever existed on
    // the strength of a thread that no longer does.
    {
        os::kernel::CapabilityTable table;
        auto root = table.mint(alice, object, read_write, true);
        auto to_bob = table.grant(alice, root.value(), bob, read_write, true);
        auto to_carol = table.grant(bob, to_bob.value(), carol, read_write, true);
        auto unrelated = table.mint(carol, object, read_only, false);
        if (!check(static_cast<bool>(to_carol) && static_cast<bool>(unrelated),
                   "setup grants refused")) return 1;
        if (!check(table.live_capability_count() == 4U, "wrong live count")) return 1;

        const std::size_t removed = table.revoke_all_held_by(bob);
        if (!check(removed == 2U, "exit did not surrender the subtree")) return 1;
        if (!check(live(table, root.value()), "exit removed a capability upstream of it")) return 1;
        if (!check(!live(table, to_bob.value()) && !live(table, to_carol.value()),
                   "exit left the dead thread's authority in place")) return 1;
        if (!check(live(table, unrelated.value()),
                   "exit removed a capability the dead thread never granted")) return 1;
        if (!check(table.count_held_by(bob) == 0U, "the dead thread still holds something")) return 1;

        // Surrendering for a thread that holds nothing is not an error, it is
        // simply no work - the common case when any thread exits.
        if (!check(table.revoke_all_held_by(mallory) == 0U,
                   "surrendering nothing reported work")) return 1;
    }

    // The table has a stated ceiling and refuses rather than overruns when it is
    // reached. There is no allocator underneath it to grow.
    {
        os::kernel::CapabilityTable table;
        for (std::size_t i = 0U; i < os::kernel::max_capabilities; ++i) {
            auto minted = table.mint(alice, object, read_write, true);
            if (!check(static_cast<bool>(minted), "mint refused below the ceiling")) return 1;
        }
        if (!check(table.live_capability_count() == os::kernel::max_capabilities,
                   "wrong live count at the ceiling")) return 1;
        if (!check(refused(table.mint(alice, object, read_write, true),
                           os::kernel::capability_errors::capability_limit),
                   "the table grew past its ceiling")) return 1;

        const std::size_t surrendered = table.revoke_all_held_by(alice);
        if (!check(surrendered == os::kernel::max_capabilities,
                   "surrendering a full table did not empty it")) return 1;
        if (!check(table.live_capability_count() == 0U, "the table did not empty")) return 1;
        if (!check(static_cast<bool>(table.mint(alice, object, read_write, true)),
                   "slots were not reusable after being surrendered")) return 1;
    }

    return 0;
}
