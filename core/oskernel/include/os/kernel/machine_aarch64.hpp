#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/kernel/aarch64_mapping_state.hpp>
#include <os/kernel/machine.hpp>

namespace os::kernel {

// Concrete EL1 kernel-thread context for the AArch64 machine layer.
//
// This is intentionally smaller than ExceptionFrame. A context switch is a
// normal AAPCS64 call boundary, so caller-clobbered registers are already dead
// from the C++ caller's point of view. User register state is preserved in the
// exception frame on the owning kernel stack. Keeping these two structures
// separate prevents every scheduler switch from copying attacker-controlled
// x0-x18 state unnecessarily.
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

// The native machine objects now hold real bounded mapping authority instead of
// empty placeholders. `MachineAddressSpace` still remains opaque above
// machine.hpp; only the AArch64 backend can inspect these fields.
struct MachinePhysicalLedger final {
    aarch64::NativePhysicalLedger mappings {};
};

struct MachineAddressSpace final {
    aarch64::NativeAddressSpaceState mappings {};
    MachinePhysicalLedger* physical_ledger {nullptr};
    aarch64::EarlyStage1Builder* early_builder {nullptr};
};

// Early boot attaches the already-initialized translation-table builder after
// binding the address space to the one machine-wide physical ledger. General VM
// ownership can replace this hook later without changing the portable contract.
[[nodiscard]] os::core::Result<void> aarch64_attach_early_stage1(
    MachineAddressSpace& space,
    aarch64::EarlyStage1Builder& builder) noexcept;

extern "C" void cookie_aarch64_switch_context(
    MachineContext* from,
    const MachineContext* to) noexcept;

} // namespace os::kernel
