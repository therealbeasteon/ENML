#include <os/kernel/boot_memory_plan.hpp>

#include <array>
#include <cstdlib>

namespace {
// Templated so a Result can be passed directly. Result's operator bool is
// explicit, which satisfies the contextual conversion in `!value` but not
// an implicit conversion to a bool parameter.
template <typename T>
void require(const T& value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;

    HardwareInventory inventory{};
    inventory.memory[0] = HardwareRange{0x40000000ULL, 0x02000000ULL};
    inventory.memory_count = 1U;
    inventory.reserved[0] = HardwareRange{0x40000000ULL, 0x00010000ULL};
    inventory.reserved_count = 1U;

    const std::array<HardwareRange, 2U> protected_ranges{
        HardwareRange{0x40010000ULL, 0x00030000ULL}, // kernel image
        HardwareRange{0x40040000ULL, 0x00010000ULL}, // DTB
    };

    auto plan = plan_early_boot_memory(
        inventory,
        protected_ranges,
        16U,
        8U,
        4096ULL);
    require(static_cast<bool>(plan));
    require(plan.value().valid());
    require(plan.value().page_tables.base == 0x40050000ULL);
    require(plan.value().page_tables.size == 16ULL * 4096ULL);
    require(plan.value().kernel_stack.base == 0x40060000ULL);
    require(plan.value().kernel_stack.size == 8ULL * 4096ULL);
    require(!ranges_overlap(plan.value().page_tables, protected_ranges[0]));
    require(!ranges_overlap(plan.value().page_tables, protected_ranges[1]));
    require(!ranges_overlap(plan.value().kernel_stack, plan.value().page_tables));

    require(!plan_early_boot_memory(inventory, protected_ranges, 0U, 8U, 4096ULL));
    require(!plan_early_boot_memory(inventory, protected_ranges, 16U, 0U, 4096ULL));

    return 0;
}
