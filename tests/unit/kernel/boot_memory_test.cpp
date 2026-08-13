#include <os/kernel/boot_memory.hpp>

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
    inventory.memory[0] = HardwareRange{0x40000000ULL, 0x01000000ULL};
    inventory.memory_count = 1U;
    inventory.reserved[0] = HardwareRange{0x40000000ULL, 0x00020000ULL};
    inventory.reserved[1] = HardwareRange{0x40040000ULL, 0x00010000ULL};
    inventory.reserved_count = 2U;

    const std::array<HardwareRange, 1U> protected_ranges{
        HardwareRange{0x40020000ULL, 0x00020000ULL},
    };

    auto first = select_early_ram(inventory, 0x10000ULL, 0x1000ULL, protected_ranges);
    require(static_cast<bool>(first));
    require(first.value().base == 0x40050000ULL);
    require(first.value().size == 0x10000ULL);

    auto aligned = select_early_ram(inventory, 0x1000ULL, 0x20000ULL, protected_ranges);
    require(static_cast<bool>(aligned));
    require((aligned.value().base & 0x1FFFFULL) == 0ULL);
    require(!ranges_overlap(aligned.value(), inventory.reserved[0]));
    require(!ranges_overlap(aligned.value(), inventory.reserved[1]));
    require(!ranges_overlap(aligned.value(), protected_ranges[0]));

    require(!select_early_ram(inventory, 0U, 0x1000ULL));
    require(!select_early_ram(inventory, 0x1000ULL, 3000ULL));

    HardwareInventory exhausted{};
    exhausted.memory[0] = HardwareRange{0x1000ULL, 0x3000ULL};
    exhausted.memory_count = 1U;
    exhausted.reserved[0] = HardwareRange{0x1000ULL, 0x3000ULL};
    exhausted.reserved_count = 1U;
    require(!select_early_ram(exhausted, 0x1000ULL, 0x1000ULL));

    return 0;
}
