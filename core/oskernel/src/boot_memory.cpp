#include <os/kernel/boot_memory.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error boot_memory_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

[[nodiscard]] constexpr bool power_of_two(std::uint64_t value) noexcept {
    return value != 0ULL && (value & (value - 1ULL)) == 0ULL;
}

[[nodiscard]] bool align_up(std::uint64_t value, std::uint64_t alignment, std::uint64_t& out) noexcept {
    const std::uint64_t mask = alignment - 1ULL;
    if (value > std::numeric_limits<std::uint64_t>::max() - mask) return false;
    out = (value + mask) & ~mask;
    return true;
}

[[nodiscard]] bool candidate_blocked(
    HardwareRange candidate,
    const HardwareInventory& inventory,
    os::core::Span<const HardwareRange> protected_ranges,
    HardwareRange& blocker) noexcept {
    for (std::size_t i = 0U; i < inventory.reserved_count; ++i) {
        if (ranges_overlap(candidate, inventory.reserved[i])) {
            blocker = inventory.reserved[i];
            return true;
        }
    }
    for (const auto& range : protected_ranges) {
        if (ranges_overlap(candidate, range)) {
            blocker = range;
            return true;
        }
    }
    return false;
}

} // namespace

os::core::Result<HardwareRange> select_early_ram(
    const HardwareInventory& inventory,
    std::uint64_t bytes,
    std::uint64_t alignment,
    os::core::Span<const HardwareRange> protected_ranges) noexcept {
    if (bytes == 0ULL || !power_of_two(alignment)) {
        return boot_memory_error(boot_memory_errors::invalid_request);
    }

    for (std::size_t memory_index = 0U; memory_index < inventory.memory_count; ++memory_index) {
        const HardwareRange memory = inventory.memory[memory_index];
        if (!memory.valid()) continue;

        std::uint64_t cursor = 0ULL;
        if (!align_up(memory.base, alignment, cursor)) continue;

        while (cursor < memory.end_exclusive()) {
            if (bytes > memory.end_exclusive() - cursor) break;
            const HardwareRange candidate{cursor, bytes};

            HardwareRange blocker{};
            if (!candidate_blocked(candidate, inventory, protected_ranges, blocker)) {
                return candidate;
            }

            // Skip directly past the overlapping reservation, then restore the
            // requested alignment. This keeps selection deterministic while
            // avoiding a page-by-page scan across large reserved regions.
            if (!blocker.valid() || blocker.end_exclusive() <= cursor) {
                return boot_memory_error(boot_memory_errors::invalid_request);
            }
            if (!align_up(blocker.end_exclusive(), alignment, cursor)) break;
        }
    }

    return boot_memory_error(boot_memory_errors::exhausted);
}

} // namespace os::kernel
