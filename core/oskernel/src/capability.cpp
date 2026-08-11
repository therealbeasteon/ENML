#include <os/kernel/capability.hpp>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error capability_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

} // namespace

CapabilityTable::Slot* CapabilityTable::find(CapabilityId capability) noexcept {
    if (capability == invalid_capability) return nullptr;
    for (auto& slot : slots_) {
        if (slot.occupied && slot.id == capability) return &slot;
    }
    return nullptr;
}

const CapabilityTable::Slot* CapabilityTable::find(CapabilityId capability) const noexcept {
    if (capability == invalid_capability) return nullptr;
    for (const auto& slot : slots_) {
        if (slot.occupied && slot.id == capability) return &slot;
    }
    return nullptr;
}

const CapabilityTable::Slot* CapabilityTable::parent_of(const Slot& slot) const noexcept {
    if (slot.parent == invalid_capability) return nullptr;
    if (slot.parent_slot >= slots_.size()) return nullptr;
    const Slot& candidate = slots_[slot.parent_slot];
    // The recorded index is a shortcut, never evidence. Slots are reused, so the
    // slot is the parent only if it still carries the id the child was derived
    // from - otherwise the index now names a stranger and the answer is "no
    // parent", which is the conservative one.
    if (!candidate.occupied || candidate.id != slot.parent) return nullptr;
    return &candidate;
}

std::size_t CapabilityTable::live_capability_count() const noexcept {
    return occupied_;
}

std::size_t CapabilityTable::count_held_by(ThreadId holder) const noexcept {
    if (holder == invalid_thread) return 0U;
    std::size_t held = 0U;
    for (const auto& slot : slots_) {
        if (slot.occupied && slot.holder == holder) ++held;
    }
    return held;
}

bool CapabilityTable::holds(ThreadId thread, CapabilityId capability) const noexcept {
    if (thread == invalid_thread) return false;
    const Slot* slot = find(capability);
    return slot != nullptr && slot->holder == thread;
}

os::core::Result<CapabilityInfo> CapabilityTable::describe(
    CapabilityId capability) const noexcept {
    if (capability == invalid_capability) {
        return os::core::Result<CapabilityInfo>{
            capability_error(capability_errors::invalid_capability_id)};
    }
    const Slot* slot = find(capability);
    if (slot == nullptr) {
        return os::core::Result<CapabilityInfo>{
            capability_error(capability_errors::unknown_capability)};
    }
    return os::core::Result<CapabilityInfo>{CapabilityInfo{
        slot->id,
        slot->parent,
        slot->object,
        slot->rights,
        slot->holder,
        slot->transferable,
        slot->depth}};
}

os::core::Result<CapabilityId> CapabilityTable::mint(
    ThreadId holder,
    ObjectId object,
    Rights rights,
    bool transferable) noexcept {
    if (holder == invalid_thread) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::invalid_holder)};
    }
    if (object == invalid_object) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::invalid_object_id)};
    }
    // Unreachable at 64 bits, and checked anyway: a counter that wraps in
    // silence is how a stale reference starts resolving to live authority.
    if (next_id_ == invalid_capability) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::identifier_exhausted)};
    }

    for (auto& slot : slots_) {
        if (slot.occupied) continue;
        slot = Slot{
            next_id_,
            invalid_capability,
            object,
            rights,
            holder,
            0U,
            transferable,
            0U,
            true,
            false};
        ++next_id_;
        ++occupied_;
        return os::core::Result<CapabilityId>{slot.id};
    }
    return os::core::Result<CapabilityId>{capability_error(capability_errors::capability_limit)};
}

os::core::Result<CapabilityId> CapabilityTable::grant(
    ThreadId granter,
    CapabilityId capability,
    ThreadId recipient,
    Rights rights,
    bool transferable) noexcept {
    if (granter == invalid_thread || recipient == invalid_thread) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::invalid_holder)};
    }
    // Granting to yourself would only ever consume a slot to hold a weaker copy
    // of something you already have, and a thread able to do it repeatedly can
    // fill the table. Attenuation does not need it: a grant attenuates on the
    // way out, in one step.
    if (granter == recipient) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::self_addressed)};
    }
    if (capability == invalid_capability) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::invalid_capability_id)};
    }

    Slot* parent = find(capability);
    if (parent == nullptr) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::unknown_capability)};
    }
    // Holding it is the authority to pass it on. There is no separate permission
    // to consult, which is the point of a capability system: the object is the
    // permission.
    if (parent->holder != granter) {
        return os::core::Result<CapabilityId>{capability_error(capability_errors::not_holder)};
    }
    // The answer to unbounded proliferation. A capability without this cannot be
    // re-granted at all, so whether a recipient becomes a distribution point is
    // its granter's decision rather than a property the system gives away.
    //
    // This also subsumes the transferability half of attenuation: a child can
    // only exist under a transferable parent, so a transferable child never
    // exceeds its parent. There is deliberately no second check for that
    // - a check that cannot fail reads as though it were load-bearing.
    if (!parent->transferable) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::not_transferable)};
    }
    // Rights may only shrink. Without this a grant could add authority, and
    // every other rule in this file would be decorative.
    if ((rights & ~parent->rights) != no_rights) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::rights_escalation)};
    }
    if (static_cast<std::size_t>(parent->depth) + 1U > max_derivation_depth) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::derivation_too_deep)};
    }
    if (next_id_ == invalid_capability) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::identifier_exhausted)};
    }

    const std::size_t parent_index = static_cast<std::size_t>(parent - slots_.data());
    const ObjectId object = parent->object;
    const std::uint8_t depth = static_cast<std::uint8_t>(parent->depth + 1U);

    for (auto& slot : slots_) {
        if (slot.occupied) continue;
        slot = Slot{
            next_id_,
            capability,
            object,
            rights,
            recipient,
            parent_index,
            transferable,
            depth,
            true,
            false};
        ++next_id_;
        ++occupied_;
        return os::core::Result<CapabilityId>{slot.id};
    }
    return os::core::Result<CapabilityId>{capability_error(capability_errors::capability_limit)};
}

std::size_t CapabilityTable::sweep_doomed() noexcept {
    // Propagate down the derivation tree. A capability is at most
    // max_derivation_depth below the deepest thing being removed, so that many
    // passes reach every descendant - and no caller can arrange for more,
    // because grant() refuses to build a chain longer than that in the first
    // place. This is the bounded-work-per-call rule from M7.4a holding in the
    // one operation here that could plausibly have broken it.
    for (std::size_t pass = 0U; pass < max_derivation_depth; ++pass) {
        bool spread = false;
        for (auto& slot : slots_) {
            if (!slot.occupied || slot.doomed) continue;
            const Slot* parent = parent_of(slot);
            if (parent == nullptr || !parent->doomed) continue;
            slot.doomed = true;
            spread = true;
        }
        if (!spread) break;
    }

    std::size_t removed = 0U;
    for (auto& slot : slots_) {
        if (!slot.occupied || !slot.doomed) continue;
        // Cleared rather than flagged. Unlike a thread slot there is nothing to
        // learn from a retired capability: its id is never reissued, so a stale
        // reference already resolves to unknown_capability rather than to
        // whatever was minted next.
        slot = Slot{};
        ++removed;
    }
    occupied_ -= removed;
    return removed;
}

os::core::Result<std::size_t> CapabilityTable::revoke(
    ThreadId revoker,
    CapabilityId capability) noexcept {
    if (revoker == invalid_thread) {
        return os::core::Result<std::size_t>{
            capability_error(capability_errors::invalid_holder)};
    }
    if (capability == invalid_capability) {
        return os::core::Result<std::size_t>{
            capability_error(capability_errors::invalid_capability_id)};
    }

    Slot* target = find(capability);
    if (target == nullptr) {
        return os::core::Result<std::size_t>{
            capability_error(capability_errors::unknown_capability)};
    }

    // Two parties may revoke, and no others.
    //
    // The holder, because surrendering your own authority - and with it whatever
    // you passed on - must always be available; a thread that cannot drop a
    // capability is a thread that cannot reduce its own attack surface.
    //
    // The holder of the parent, because that is what taking back a grant means.
    // Note what is *not* here: the holder of a sibling has no say, which is the
    // difference between selective revocation and the all-or-nothing kind the
    // references describe as a defect of capability systems generally.
    const bool by_holder = target->holder == revoker;
    const Slot* parent = parent_of(*target);
    const bool by_grantor = parent != nullptr && parent->holder == revoker;
    if (!by_holder && !by_grantor) {
        return os::core::Result<std::size_t>{
            capability_error(capability_errors::not_revocable)};
    }

    target->doomed = true;
    return os::core::Result<std::size_t>{sweep_doomed()};
}

std::size_t CapabilityTable::revoke_all_held_by(ThreadId holder) noexcept {
    if (holder == invalid_thread) return 0U;

    // No permission is asked for, because this is not a thread exercising
    // authority - it is the kernel meeting an obligation for a thread that no
    // longer exists. The rendezvous has the same shape: exiting releases
    // everyone blocked on you whether or not anybody asked.
    bool any = false;
    for (auto& slot : slots_) {
        if (!slot.occupied || slot.holder != holder) continue;
        slot.doomed = true;
        any = true;
    }
    if (!any) return 0U;
    return sweep_doomed();
}

} // namespace os::kernel
