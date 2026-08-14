#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/kernel/aarch64_mapping_state.hpp>
#include <os/kernel/address_space_epoch.hpp>
#include <os/kernel/machine.hpp>

namespace os::kernel {

struct alignas(16) MachineContext final {
    std::array<std::uint64_t, 11U> x19_to_x29 {};
    std::uint64_t x30 {0U};
    std::uint64_t sp {0U};
    bool prepared {false};
};

static_assert(offsetof(MachineContext, x19_to_x29) == 0U);
static_assert(offsetof(MachineContext, x30) == 88U);
static_assert(offsetof(MachineContext, sp) == 96U);
static_assert(alignof(MachineContext) == 16U);

struct MachinePhysicalLedger final {
    aarch64::NativePhysicalLedger mappings {};
};

struct MachineAddressSpace final {
    aarch64::NativeAddressSpaceState mappings {};
    MachinePhysicalLedger* physical_ledger {nullptr};
    aarch64::EarlyStage1Builder* early_builder {nullptr};
};

[[nodiscard]] os::core::Result<void> aarch64_attach_early_stage1(
    MachineAddressSpace& space,
    aarch64::EarlyStage1Builder& builder) noexcept;

// Declares a physical range to hold kernel state, owned by `space`. See
// NativePhysicalReservation for what that forbids, PhysicalReservationKind for
// what the two kinds differ on, and why the owner is an address space.
[[nodiscard]] os::core::Result<void> aarch64_reserve_physical(
    MachineAddressSpace& space,
    std::uintptr_t physical_base,
    std::size_t length,
    aarch64::PhysicalReservationKind kind) noexcept;

// Bulk teardown of a space whose epoch has already begun retiring.
//
// The retiring token is the evidence, and taking it is the whole reason this
// exists beside machine_release_address_space rather than inside it.
// begin_retire() has already invalidated the software epoch, so
// ProcessTranslationTable::resolve can no longer produce a binding and no
// thread can be scheduled into this space. The portable signature in
// machine.hpp takes only the space, and therefore cannot express that
// precondition at all - it asks the machine layer to prove a property from an
// argument that does not carry it.
//
// Unmaps every mapping through the ordinary TLBI-backed path, drops the
// space's reservations, retires the ASID, and unbinds. Cookie is single-CPU
// today; the token is what makes this interface still correct when it is not,
// because "no CPU is executing here" becomes a fact the caller establishes
// rather than one this function could ever check locally.
[[nodiscard]] os::core::Result<void> aarch64_release_address_space(
    MachineAddressSpace& space,
    RetiringAddressSpaceEpoch retiring) noexcept;

// Hands one page a process already holds authority over to `space`, for
// translation structures.
//
// This is the operation that makes address-space creation possible after boot:
// the kernel has no pool to draw from, so the caller supplies the page. It is
// also the operation with the sharpest failure mode, and the enforcement is a
// composition rather than a new check - the page is first reserved as
// kernel_object owned by this space, and `reserve_physical` refuses a range any
// process can still reach. A donor that has not unmapped its own page is
// therefore rejected here, before the page becomes a table it could still
// write. Reserving *before* donating is the whole ordering: the reverse would
// leave a window in which the page is a live table and still mappable.
[[nodiscard]] os::core::Result<void> aarch64_donate_table_page(
    MachineAddressSpace& space,
    aarch64::EarlyPageArena& arena,
    std::uintptr_t physical) noexcept;

[[nodiscard]] os::core::Result<void> aarch64_map_user(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::uintptr_t physical_base,
    std::size_t length,
    MachinePermissions permissions) noexcept;

[[nodiscard]] os::core::Result<void> aarch64_map_user_stack(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::uintptr_t physical_base,
    std::size_t length) noexcept;

[[nodiscard]] os::core::Result<void> aarch64_validate_user_context(
    MachineAddressSpace& space,
    std::uintptr_t entry,
    std::uintptr_t stack) noexcept;

extern "C" void cookie_aarch64_switch_context(
    MachineContext* from,
    const MachineContext* to) noexcept;

extern "C" [[noreturn]] void cookie_aarch64_enter_el0(
    std::uint64_t entry,
    std::uint64_t stack) noexcept;

} // namespace os::kernel
