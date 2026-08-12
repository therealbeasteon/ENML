#include <os/kernel/boot_memory_plan.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error plan_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

[[nodiscard]] bool bytes_for_pages(
    std::size_t pages,
    std::uint64_t page_size,
    std::uint64_t& bytes) noexcept {
    if (pages == 0U || page_size == 0ULL) return false;
    if (static_cast<std::uint64_t>(pages) >
        std::numeric_limits<std::uint64_t>::max() / page_size) return false;
    bytes = static_cast<std::uint64_t>(pages) * page_size;
    return true;
}

} // namespace

os::core::Result<EarlyBootMemoryPlan> plan_early_boot_memory(
    const HardwareInventory& inventory,
    os::core::Span<const HardwareRange> protected_ranges,
    std::size_t page_table_pages,
    std::size_t stack_pages,
    std::uint64_t page_size) noexcept {
    std::uint64_t table_bytes = 0ULL;
    std::uint64_t stack_bytes = 0ULL;
    if (!bytes_for_pages(page_table_pages, page_size, table_bytes) ||
        !bytes_for_pages(stack_pages, page_size, stack_bytes)) {
        return plan_error(boot_memory_errors::invalid_request);
    }

    auto tables = select_early_ram(inventory, table_bytes, page_size, protected_ranges);
    if (!tables) return tables.error();

    // The temporary exclusion list is fixed-size because boot planning occurs
    // before the heap exists. Protected ranges are already externally bounded
    // by their owner; the one extra slot is the freshly selected table arena.
    constexpr std::size_t max_boot_protected = 32U;
    if (protected_ranges.size() >= max_boot_protected) {
        return plan_error(boot_memory_errors::exhausted);
    }
    std::array<HardwareRange, max_boot_protected> exclusions{};
    std::size_t count = 0U;
    for (const auto& range : protected_ranges) exclusions[count++] = range;
    exclusions[count++] = tables.value();

    auto stack = select_early_ram(
        inventory,
        stack_bytes,
        page_size,
        os::core::Span<const HardwareRange>{exclusions.data(), count});
    if (!stack) return stack.error();

    const EarlyBootMemoryPlan plan{
        .page_tables = tables.value(),
        .kernel_stack = stack.value(),
    };
    if (!plan.valid()) return plan_error(boot_memory_errors::invalid_request);
    return plan;
}

} // namespace os::kernel
