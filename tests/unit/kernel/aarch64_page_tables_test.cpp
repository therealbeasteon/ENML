#include <os/kernel/aarch64_page_tables.hpp>

#include <array>
#include <cstdlib>
#include <cstdint>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;
    using namespace os::kernel::aarch64;

    alignas(4096) std::array<std::byte, 5U * 4096U> storage{};
    const auto begin = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(storage.data()));
    const auto end = begin + storage.size();

    EarlyPageArena arena{begin, end};
    require(arena.valid());
    require(arena.remaining_pages() == 5U);

    EarlyStage1Builder builder{arena};
    auto root = builder.initialize();
    require(static_cast<bool>(root));
    require(root.value() == begin);
    require(builder.remaining_table_pages() == 4U);

    constexpr std::uint64_t va = 0x0000'0000'4000'0000ULL;
    constexpr std::uint64_t pa = 0x0000'0000'8000'0000ULL;
    auto before = builder.mapped(va);
    require(static_cast<bool>(before));
    require(!before.value());

    auto mapped = builder.map_page(
        va, pa, MachinePermissions::read_execute, MachineMemoryKind::normal);
    require(static_cast<bool>(mapped));
    require(builder.remaining_table_pages() == 2U);

    auto second = builder.map_page(
        va + 4096ULL,
        pa + 4096ULL,
        MachinePermissions::read_write,
        MachineMemoryKind::normal);
    require(static_cast<bool>(second));
    require(builder.remaining_table_pages() == 2U);

    auto removed = builder.unmap_page(va);
    require(static_cast<bool>(removed));
    auto after_remove = builder.mapped(va);
    require(static_cast<bool>(after_remove));
    require(!after_remove.value());
    // Intermediate translation tables remain allocated in the monotonic early
    // regime; removing a leaf never fabricates reusable physical memory.
    require(builder.remaining_table_pages() == 2U);

    auto remove_again = builder.unmap_page(va);
    require(!remove_again);
    require(remove_again.error().code == machine_errors::not_mapped);

    auto remapped = builder.map_page(
        va, pa, MachinePermissions::read_execute, MachineMemoryKind::normal);
    require(static_cast<bool>(remapped));
    require(builder.remaining_table_pages() == 2U);

    auto bad_device_exec = builder.map_page(
        va + 8192ULL,
        0x0000'0000'0900'0000ULL,
        MachinePermissions::read_execute,
        MachineMemoryKind::device);
    require(!bad_device_exec);

    auto* l1 = reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(root.value()));
    const auto l2_pa = l1[level1_index(va)] & page_address_mask;
    auto* l2 = reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(l2_pa));
    const auto l3_pa = l2[level2_index(va)] & page_address_mask;
    auto* l3 = reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(l3_pa));
    const auto leaf = l3[level3_index(va)];
    require((leaf & page_address_mask) == pa);
    require((leaf & descriptor::ap_read_only_el1) != 0ULL);
    require((leaf & descriptor::privileged_execute_never) == 0ULL);
    require((leaf & descriptor::unprivileged_execute_never) != 0ULL);

    return 0;
}
