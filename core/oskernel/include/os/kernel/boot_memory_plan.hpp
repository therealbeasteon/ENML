#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/boot_memory.hpp>

namespace os::kernel {

struct EarlyBootMemoryPlan final {
    HardwareRange page_tables {};
    HardwareRange kernel_stack {};

    [[nodiscard]] bool valid() const noexcept {
        return page_tables.valid() && kernel_stack.valid() &&
               !ranges_overlap(page_tables, kernel_stack);
    }
};

// Carves the first two machine-owned boot regions from safe RAM while treating
// the kernel image, DTB, firmware reservations and caller-owned boot objects as
// unavailable. `page_table_pages` and `stack_pages` are deliberately explicit:
// changing them changes the early trusted-memory budget and must be reviewed.
[[nodiscard]] os::core::Result<EarlyBootMemoryPlan> plan_early_boot_memory(
    const HardwareInventory& inventory,
    os::core::Span<const HardwareRange> protected_ranges,
    std::size_t page_table_pages,
    std::size_t stack_pages,
    std::uint64_t page_size) noexcept;

} // namespace os::kernel
