#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/rendezvous.hpp>

// The Cookie Kernel's capability transfer core, as a state machine.
//
// This is ENML's addition to the four kernel services the references
// demonstrate, and `docs/M7_0_KERNEL.md` states why it has to be a kernel
// primitive rather than a convention: the entire system above treats authority
// as an object that is granted, brokered and revoked, and a convention is
// exactly what an attacker with a memory-safety bug ignores.
//
// Like the rendezvous it is written as pure logic - no address spaces, no
// scheduling, no hardware - so the rules below can be tested on a development
// host years before a board exists. `docs/M7_1_CAPABILITY.md` records the
// reasoning; what follows is the part that has to be true in code.
//
// The references describe two families of capability system, and ENML is
// deliberately in the first. Capabilities named by kernel-held indices cannot be
// forged by their holder; capabilities protected by a one-way function can be
// held anywhere but can only be checked by recomputing it. The second exists to
// solve a *distributed* trust problem - a server that cannot trust the kernel
// holding the capability - which a phone does not have. Choosing it here would
// buy nothing and cost two things: a cryptographic operation on a path the
// references specifically warn must not cost hundreds of clock cycles on a
// battery-powered device, and both of the defects below.
//
// Those defects are the reason this file exists in the shape it does. The
// references state them as properties of capability systems in general:
//
//   - **Revocation is all-or-nothing.** Invalidating a check value invalidates
//     every outstanding capability for that object at once, so taking back what
//     was given to one holder means taking it back from everyone. This is
//     recorded in the literature as a defect of *all* capability systems.
//
//   - **Proliferation is uncontrolled.** Nothing stops the holder of a valid
//     capability from copying it to everyone it can reach, and the references
//     note that a kernel-managed table is what solves this.
//
// ENML cannot inherit either. M2.3 already claims deterministic revocation at
// the service layer, and a kernel weaker than the service built on top of it
// would make that claim false from underneath. So:
//
//   - **Every capability records what it was derived from.** A grant creates a
//     child of the capability it came from, and revoking a capability removes it
//     together with its entire derivation subtree and nothing else. That is
//     selective revocation: taking back what one holder was given also takes
//     back whatever that holder passed on, and touches no sibling. The
//     derivation tree is the whole mechanism.
//
//   - **Passing a capability on is itself a right.** A capability that is not
//     transferable cannot be granted onward, so whether a recipient becomes a
//     distribution point is a decision its granter makes rather than a property
//     the system hands out for free.
//
//   - **Attenuation only ever attenuates.** A derived capability's rights are a
//     subset of its parent's and its transferability cannot exceed its parent's.
//     Enforced here rather than trusted to callers, which is the same
//     server-authoritative rights reduction M2.1 already applies one layer up.
//
//   - **A dead thread holds nothing.** Exiting surrenders everything the thread
//     held and everything derived from it, for the same reason the rendezvous
//     releases everyone blocked on a thread that dies: an unrecoverable
//     obligation the kernel is the only party able to meet.
//
// Every operation here does a bounded number of passes over a fixed table, with
// no loop count a caller can influence - the rule `docs/M7_4_KERNEL_HARDENING.md`
// puts on every kernel call, checked here rather than after the fact.
namespace os::kernel {

// Ceilings, not capacity targets. As with the thread table, knowing an exact
// upper bound is what lets every structure here be a fixed array with no
// allocator underneath it - and per M7.4a a kernel with no heap has nothing to
// exhaust and no allocation-failure path to get wrong.
inline constexpr std::size_t max_capabilities = 256U;

// How far a capability may be re-granted from the root that minted it.
//
// A ceiling on delegation depth is worth having on its own - a chain longer than
// this is one nobody can audit - but it is also what makes revocation bounded.
// Removing a derivation subtree takes this many propagation passes and no more,
// so the cost of a revoke is a compile-time constant rather than a function of
// how much delegation an attacker arranged first.
inline constexpr std::size_t max_derivation_depth = 8U;

// Zero is never a valid capability. A zeroed register must not name one.
//
// Sixty-four bits, and never reused once retired. The kernel mints these, so
// unlike a thread id they are not a caller's choice to keep distinct, and a
// recycled identifier would let a stale reference resolve to whatever authority
// happened to be minted next - the same hazard the rendezvous avoids by
// retaining exited slots. A 32-bit counter is reachable in weeks at a rate a
// busy phone can sustain; a 64-bit one is not reachable at all.
using CapabilityId = std::uint64_t;
inline constexpr CapabilityId invalid_capability = 0U;

// What the capability is authority *over*. Opaque: the kernel stores it, matches
// it and hands it back, and never interprets it. Which object a number names is
// a decision belonging to whichever service minted the capability, and a kernel
// that knew would be a kernel that had opinions about storage and displays.
using ObjectId = std::uint64_t;
inline constexpr ObjectId invalid_object = 0U;

// The rights a capability carries, as an opaque bitmask.
//
// Opaque in the same sense: the kernel only ever *intersects* these, and
// intersection is meaningful without knowing what any bit means. That is the
// split that lets attenuation be enforced by a kernel that has no idea what it
// is attenuating.
using Rights = std::uint32_t;
inline constexpr Rights no_rights = 0U;
inline constexpr Rights all_rights = 0xFFFFFFFFU;

namespace capability_errors {
inline constexpr std::uint32_t invalid_capability_id = 1U;
inline constexpr std::uint32_t unknown_capability = 2U;
inline constexpr std::uint32_t capability_limit = 3U;
// The caller does not hold the capability it is trying to use.
inline constexpr std::uint32_t not_holder = 4U;
// The capability may not be passed on.
inline constexpr std::uint32_t not_transferable = 5U;
// The derived rights are not a subset of the parent's, or the child would be
// transferable when its parent is not.
inline constexpr std::uint32_t rights_escalation = 6U;
inline constexpr std::uint32_t derivation_too_deep = 7U;
inline constexpr std::uint32_t self_addressed = 8U;
inline constexpr std::uint32_t invalid_object_id = 9U;
// Neither the holder nor the holder of the parent, so not entitled to revoke.
inline constexpr std::uint32_t not_revocable = 10U;
// The identifier space is exhausted. Unreachable in practice at 64 bits, and
// present because a counter that wraps silently is how a stale reference starts
// resolving to live authority.
inline constexpr std::uint32_t identifier_exhausted = 11U;
// A capability held by nobody is authority nobody is accountable for.
inline constexpr std::uint32_t invalid_holder = 12U;
} // namespace capability_errors

// What a capability is, as seen from outside the kernel. Returned by value;
// nothing above the kernel holds a pointer into the table.
struct CapabilityInfo final {
    CapabilityId id {invalid_capability};
    CapabilityId parent {invalid_capability};
    ObjectId object {invalid_object};
    Rights rights {no_rights};
    ThreadId holder {invalid_thread};
    bool transferable {false};
    std::uint8_t depth {0U};

    [[nodiscard]] friend constexpr bool
    operator==(const CapabilityInfo&, const CapabilityInfo&) = default;
};

class CapabilityTable final {
public:
    CapabilityTable() noexcept = default;

    // Creates authority that did not previously exist.
    //
    // This is the operation `CallAuthority::capability_control` in the ABI
    // guards, and the only one that needs guarding that way: everything else
    // here is gated on a capability the caller already holds, so within this
    // state machine there is no privilege to escalate *to*. A root has no
    // parent, and so nobody above it who could take it back.
    [[nodiscard]] os::core::Result<CapabilityId> mint(
        ThreadId holder,
        ObjectId object,
        Rights rights,
        bool transferable) noexcept;

    // Derives a child of an existing capability into another thread.
    //
    // Refused unless the granter holds the capability, the capability is
    // transferable, and the requested rights and transferability are no greater
    // than the parent's. The attenuation check is the point: a grant is the only
    // way authority moves, so a grant that could add rights would make every
    // other rule here decorative.
    [[nodiscard]] os::core::Result<CapabilityId> grant(
        ThreadId granter,
        CapabilityId capability,
        ThreadId recipient,
        Rights rights,
        bool transferable) noexcept;

    // Removes a capability and everything derived from it.
    //
    // Permitted to the holder - surrendering your own authority, and with it
    // whatever you passed on - and to the holder of its parent, which is what
    // "take back what I gave you" means. Not permitted to anyone else, including
    // the holder of a sibling.
    //
    // Returns how many capabilities were removed, so a caller can observe that
    // the subtree actually went rather than trusting that it did.
    os::core::Result<std::size_t> revoke(ThreadId revoker, CapabilityId capability) noexcept;

    // Surrenders everything a thread holds, and everything derived from it.
    //
    // Called when a thread exits. Returns how many capabilities were removed.
    // Unlike revoke() this asks no permission, because it is not a thread
    // exercising authority - it is the kernel meeting an obligation on behalf of
    // a thread that no longer exists.
    std::size_t revoke_all_held_by(ThreadId holder) noexcept;

    [[nodiscard]] os::core::Result<CapabilityInfo> describe(CapabilityId capability) const noexcept;

    // Whether this thread holds this capability. The question every service
    // above the kernel actually asks, answered without the caller having to
    // handle a descriptor it has no use for.
    [[nodiscard]] bool holds(ThreadId thread, CapabilityId capability) const noexcept;

    [[nodiscard]] std::size_t live_capability_count() const noexcept;
    [[nodiscard]] std::size_t count_held_by(ThreadId holder) const noexcept;

private:
    struct Slot final {
        CapabilityId id {invalid_capability};
        CapabilityId parent {invalid_capability};
        ObjectId object {invalid_object};
        Rights rights {no_rights};
        ThreadId holder {invalid_thread};
        // Where the parent lives, so walking the derivation tree does not mean
        // searching for it. Never trusted on its own: a slot is the parent only
        // if it is occupied *and* still carries the recorded parent id, because
        // slots are reused and an index alone would eventually name a stranger.
        std::size_t parent_slot {0U};
        bool transferable {false};
        std::uint8_t depth {0U};
        bool occupied {false};
        // Marked for removal during a revoke, and cleared by the same call.
        // Carrying the mark in the table is what lets a subtree be removed
        // without any scratch memory - a kernel stack is not the place to put a
        // list whose length depends on how much delegation happened.
        bool doomed {false};
    };

    [[nodiscard]] Slot* find(CapabilityId capability) noexcept;
    [[nodiscard]] const Slot* find(CapabilityId capability) const noexcept;
    [[nodiscard]] const Slot* parent_of(const Slot& slot) const noexcept;

    // Propagates the doomed marks down the derivation tree and then clears every
    // marked slot, returning how many went.
    //
    // Exactly max_derivation_depth propagation passes over a fixed table: a
    // capability is at most that far below the deepest thing being removed, so
    // that many passes reach all of them, and no caller can arrange for more.
    // Both revoke paths seed the marks and then call this, so there is one
    // bounded algorithm here rather than two that have to agree.
    std::size_t sweep_doomed() noexcept;

    std::array<Slot, max_capabilities> slots_ {};
    std::size_t occupied_ {0U};
    // Never decremented, never reused. See CapabilityId.
    CapabilityId next_id_ {1U};
};

} // namespace os::kernel
