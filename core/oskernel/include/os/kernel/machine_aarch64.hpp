#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/kernel/aarch64_mapping_state.hpp>
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

// M7.5f bring-up seam for EL0 mappings. This is intentionally AArch64-local
// until the first user process proves the hardware semantics; the portable VM
// contract will absorb it only after those semantics are validated.
[[nodiscard]] os::core::Result<void> aarch64_map_user(
    MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::uintptr_t physical_base,
    std::size_t length,
    MachinePermissions permissions) noexcept;

extern "C" void cookie_aarch64_switch_context(
    MachineContext* from,
    const MachineContext* to) noexcept;

} // namespace os::kernel
