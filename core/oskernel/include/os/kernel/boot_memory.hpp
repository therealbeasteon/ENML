#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/kernel/hardware_inventory.hpp>

namespace os::kernel {

namespace boot_memory_errors {
inline constexpr std::uint32_t invalid_request = 80U;
inline constexpr std::uint32_t exhausted = 81U;
} // namespace boot_memory_errors

// Returns the first deterministic page-aligned physical range that is wholly
// contained in discovered RAM and overlaps neither firmware/device reservations
// nor caller-supplied protected ranges (kernel image, DTB, boot stack, etc.).
// No heap, randomness, or hidden board constants are involved.
[[nodiscard]] os::core::Result<HardwareRange> select_early_ram(
    const HardwareInventory& inventory,
    std::uint64_t bytes,
    std::uint64_t alignment,
    os::core::Span<const HardwareRange> protected_ranges = {}) noexcept;

[[nodiscard]] constexpr bool ranges_overlap(
    HardwareRange left,
    HardwareRange right) noexcept {
    return left.valid() && right.valid() &&
        left.base < right.end_exclusive() && right.base < left.end_exclusive();
}

} // namespace os::kernel
