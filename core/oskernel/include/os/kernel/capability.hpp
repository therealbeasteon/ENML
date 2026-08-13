#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/execution_authority.hpp>
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
// Cookie uses a kernel-held capability table with bounded delegation depth,
// selective subtree revocation, explicit transferability, monotonic rights
// attenuation, fixed storage, and no allocator. M7.8 adds a second property:
// process-facing capabilities may be bound to ExecutionAuthority, so reusing a
// numeric ThreadId or hardware ASID cannot transfer authority to a later process
// incarnation. Legacy ThreadId-only operations remain temporarily for the
// already-built kernel paths, but they deliberately do not authorize a
// context-bound capability.
namespace os::kernel {

inline constexpr std::size_t max_capabilities = 256U;
inline constexpr std::size_t max_derivation_depth = 8U;

using CapabilityId = std::uint64_t;
inline constexpr CapabilityId invalid_capability = 0U;
using ObjectId = std::uint64_t;
inline constexpr ObjectId invalid_object = 0U;
using Rights = std::uint32_t;
inline constexpr Rights no_rights = 0U;
inline constexpr Rights all_rights = 0xFFFFFFFFU;

namespace capability_errors {
inline constexpr std::uint32_t invalid_capability_id = 1U;
inline constexpr std::uint32_t unknown_capability = 2U;
inline constexpr std::uint32_t capability_limit = 3U;
inline constexpr std::uint32_t not_holder = 4U;
inline constexpr std::uint32_t not_transferable = 5U;
inline constexpr std::uint32_t rights_escalation = 6U;
inline constexpr std::uint32_t derivation_too_deep = 7U;
inline constexpr std::uint32_t self_addressed = 8U;
inline constexpr std::uint32_t invalid_object_id = 9U;
inline constexpr std::uint32_t not_revocable = 10U;
inline constexpr std::uint32_t identifier_exhausted = 11U;
inline constexpr std::uint32_t invalid_holder = 12U;
} // namespace capability_errors

struct CapabilityInfo final {
    CapabilityId id {invalid_capability};
    CapabilityId parent {invalid_capability};
    ObjectId object {invalid_object};
    Rights rights {no_rights};
    ThreadId holder {invalid_thread};
    bool transferable {false};
    std::uint8_t depth {0U};
    AddressSpaceIdentity holder_address_space {};
    bool context_bound {false};

    [[nodiscard]] constexpr ExecutionAuthority holder_authority() const noexcept {
        if (!context_bound) return {};
        return ExecutionAuthority{holder, holder_address_space};
    }

    [[nodiscard]] friend constexpr bool
    operator==(const CapabilityInfo&, const CapabilityInfo&) = default;
};

class CapabilityTable final {
public:
    CapabilityTable() noexcept = default;

    // Legacy kernel-internal APIs. They cannot exercise context-bound slots.
    [[nodiscard]] os::core::Result<CapabilityId> mint(
        ThreadId holder,
        ObjectId object,
        Rights rights,
        bool transferable) noexcept;
    [[nodiscard]] os::core::Result<CapabilityId> grant(
        ThreadId granter,
        CapabilityId capability,
        ThreadId recipient,
        Rights rights,
        bool transferable) noexcept;
    os::core::Result<std::size_t> revoke(
        ThreadId revoker,
        CapabilityId capability) noexcept;
    [[nodiscard]] bool holds(ThreadId thread, CapabilityId capability) const noexcept;

    // M7.8 process-facing APIs. Exact thread + address-space generation must
    // match; ASID is absent by construction.
    [[nodiscard]] os::core::Result<CapabilityId> mint(
        ExecutionAuthority holder,
        ObjectId object,
        Rights rights,
        bool transferable) noexcept;
    [[nodiscard]] os::core::Result<CapabilityId> grant(
        ExecutionAuthority granter,
        CapabilityId capability,
        ExecutionAuthority recipient,
        Rights rights,
        bool transferable) noexcept;
    os::core::Result<std::size_t> revoke(
        ExecutionAuthority revoker,
        CapabilityId capability) noexcept;
    [[nodiscard]] bool holds(
        ExecutionAuthority authority,
        CapabilityId capability) const noexcept;

    // Administrative teardown by numeric thread intentionally removes both
    // legacy and context-bound holdings. Thread destruction is a kernel action,
    // not an authority claim made by a process.
    std::size_t revoke_all_held_by(ThreadId holder) noexcept;

    [[nodiscard]] os::core::Result<CapabilityInfo> describe(
        CapabilityId capability) const noexcept;
    [[nodiscard]] std::size_t live_capability_count() const noexcept;
    [[nodiscard]] std::size_t count_held_by(ThreadId holder) const noexcept;

private:
    struct Slot final {
        CapabilityId id {invalid_capability};
        CapabilityId parent {invalid_capability};
        ObjectId object {invalid_object};
        Rights rights {no_rights};
        ThreadId holder {invalid_thread};
        std::size_t parent_slot {0U};
        bool transferable {false};
        std::uint8_t depth {0U};
        bool occupied {false};
        bool doomed {false};
        AddressSpaceIdentity holder_address_space {};
        bool context_bound {false};
    };

    [[nodiscard]] Slot* find(CapabilityId capability) noexcept;
    [[nodiscard]] const Slot* find(CapabilityId capability) const noexcept;
    [[nodiscard]] const Slot* parent_of(const Slot& slot) const noexcept;
    [[nodiscard]] static constexpr bool held_by(
        const Slot& slot,
        ExecutionAuthority authority) noexcept {
        return slot.context_bound && authority.valid() &&
               slot.holder == authority.thread &&
               slot.holder_address_space == authority.address_space;
    }

    std::size_t sweep_doomed() noexcept;

    std::array<Slot, max_capabilities> slots_ {};
    std::size_t occupied_ {0U};
    CapabilityId next_id_ {1U};
};

} // namespace os::kernel
