#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/kernel/machine.hpp>

// The host machine layer.
//
// Not a simulator and not a placeholder to be thrown away. Its purpose is to
// make the rules in docs/M7_4_KERNEL_HARDENING.md enforceable *now*, on a
// development host, rather than deferred to whenever hardware exists - because
// a kernel hardened after it is written is one that was written wrong.
//
// So the parts of the machine contract that are pure policy are implemented for
// real here: alignment, overlap, range validity, and the mapping rules. The
// parts that genuinely require a machine - switching a register file, walking a
// page table - report `unsupported` rather than pretending, because a host
// implementation that fakes a context switch would let the portable kernel be
// tested against behaviour no real machine has.
//
// The AArch64 implementation must satisfy the same tests. That is the point of
// the tests living against the contract rather than against this file.
namespace os::kernel {

// A ceiling, so the host layer needs no allocator either - the same discipline
// the kernel itself is held to.
inline constexpr std::size_t max_host_mappings = 64U;

struct HostMapping final {
    std::uintptr_t virtual_base {0};
    std::uintptr_t physical_base {0};
    std::size_t length {0};
    MachinePermissions permissions {MachinePermissions::read};
    MachineMemoryKind kind {MachineMemoryKind::normal};
    // Established by machine_map_kernel_stack, and the only kind of range a
    // context may be prepared on.
    bool kernel_stack {false};
    bool occupied {false};
};

// The host's concrete address space. Declared here rather than in machine.hpp
// so the portable kernel still only ever sees an incomplete type: it stores
// handles and hands them back, and the moment it can read one the portability
// claim is gone.
struct MachineAddressSpace final {
    std::array<HostMapping, max_host_mappings> mappings {};
    std::size_t occupied {0};
};

// The host has no register file to save, so this exists to be handed around and
// rejected by the operations that would need a real one.
struct MachineContext final {
    std::uintptr_t entry {0};
    std::uintptr_t stack {0};
    bool prepared {false};
};

// Test and inspection helpers. Deliberately not part of the portable contract -
// nothing above the machine layer may look inside an address space.
[[nodiscard]] std::size_t host_mapping_count(const MachineAddressSpace& space) noexcept;
[[nodiscard]] bool host_range_mapped(
    const MachineAddressSpace& space,
    std::uintptr_t virtual_base,
    std::size_t length) noexcept;

} // namespace os::kernel
