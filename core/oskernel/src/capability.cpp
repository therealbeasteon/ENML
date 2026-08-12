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
    // The compatibility API must never become a bypass around M7.8. A bound
    // capability can only be exercised through exact ExecutionAuthority.
    return slot != nullptr && !slot->context_bound && slot->holder == thread;
}

bool CapabilityTable::holds(
    ExecutionAuthority authority,
    CapabilityId capability) const noexcept {
    if (!authority.valid()) return false;
    const Slot* slot = find(capability);
    return slot != nullptr && held_by(*slot, authority);
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
        slot->depth,
        slot->holder_address_space,
        slot->context_bound}};
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
    if (next_id_ == invalid_capability) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::identifier_exhausted)};
    }

    for (auto& slot : slots_) {
        if (slot.occupied) continue;
        slot = Slot{
            next_id_, invalid_capability, object, rights, holder, 0U,
            transferable, 0U, true, false};
        ++next_id_;
        ++occupied_;
        return os::core::Result<CapabilityId>{slot.id};
    }
    return os::core::Result<CapabilityId>{
        capability_error(capability_errors::capability_limit)};
}

os::core::Result<CapabilityId> CapabilityTable::mint(
    ExecutionAuthority holder,
    ObjectId object,
    Rights rights,
    bool transferable) noexcept {
    if (!holder.valid()) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::invalid_holder)};
    }
    if (object == invalid_object) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::invalid_object_id)};
    }
    if (next_id_ == invalid_capability) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::identifier_exhausted)};
    }

    for (auto& slot : slots_) {
        if (slot.occupied) continue;
        slot = Slot{
            next_id_, invalid_capability, object, rights, holder.thread, 0U,
            transferable, 0U, true, false, holder.address_space, true};
        ++next_id_;
        ++occupied_;
        return os::core::Result<CapabilityId>{slot.id};
    }
    return os::core::Result<CapabilityId>{
        capability_error(capability_errors::capability_limit)};
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
    if (parent->context_bound || parent->holder != granter) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::not_holder)};
    }
    if (!parent->transferable) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::not_transferable)};
    }
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
            next_id_, capability, object, rights, recipient, parent_index,
            transferable, depth, true, false};
        ++next_id_;
        ++occupied_;
        return os::core::Result<CapabilityId>{slot.id};
    }
    return os::core::Result<CapabilityId>{
        capability_error(capability_errors::capability_limit)};
}

os::core::Result<CapabilityId> CapabilityTable::grant(
    ExecutionAuthority granter,
    CapabilityId capability,
    ExecutionAuthority recipient,
    Rights rights,
    bool transferable) noexcept {
    if (!granter.valid() || !recipient.valid()) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::invalid_holder)};
    }
    if (granter.thread == recipient.thread) {
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
    if (!held_by(*parent, granter)) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::not_holder)};
    }
    if (!parent->transferable) {
        return os::core::Result<CapabilityId>{
            capability_error(capability_errors::not_transferable)};
    }
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
            next_id_, capability, object, rights, recipient.thread, parent_index,
            transferable, depth, true, false, recipient.address_space, true};
        ++next_id_;
        ++occupied_;
        return os::core::Result<CapabilityId>{slot.id};
    }
    return os::core::Result<CapabilityId>{
        capability_error(capability_errors::capability_limit)};
}

std::size_t CapabilityTable::sweep_doomed() noexcept {
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

    const bool by_holder = !target->context_bound && target->holder == revoker;
    const Slot* parent = parent_of(*target);
    const bool by_grantor = parent != nullptr && !parent->context_bound &&
                            parent->holder == revoker;
    if (!by_holder && !by_grantor) {
        return os::core::Result<std::size_t>{
            capability_error(capability_errors::not_revocable)};
    }

    target->doomed = true;
    return os::core::Result<std::size_t>{sweep_doomed()};
}

os::core::Result<std::size_t> CapabilityTable::revoke(
    ExecutionAuthority revoker,
    CapabilityId capability) noexcept {
    if (!revoker.valid()) {
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

    const bool by_holder = held_by(*target, revoker);
    const Slot* parent = parent_of(*target);
    const bool by_grantor = parent != nullptr && held_by(*parent, revoker);
    if (!by_holder && !by_grantor) {
        return os::core::Result<std::size_t>{
            capability_error(capability_errors::not_revocable)};
    }

    target->doomed = true;
    return os::core::Result<std::size_t>{sweep_doomed()};
}

std::size_t CapabilityTable::revoke_all_held_by(ThreadId holder) noexcept {
    if (holder == invalid_thread) return 0U;

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
