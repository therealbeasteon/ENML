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

// Declares a physical range to be kernel state owned by `space`. See
// NativePhysicalReservation for what that forbids and why the owner is an
// address space.
[[nodiscard]] os::core::Result<void> aarch64_reserve_kernel_object(
    MachineAddressSpace& space,
    std::uintptr_t physical_base,
    std::size_t length) noexcept;

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
