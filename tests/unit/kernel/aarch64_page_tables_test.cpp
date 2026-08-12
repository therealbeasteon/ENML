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
    require(arena.remaining_pages() == 4U);

    constexpr std::uint64_t va = 0x0000'0000'4000'0000ULL;
    constexpr std::uint64_t pa = 0x0000'0000'8000'0000ULL;
    auto mapped = builder.map_page(
        va, pa, MachinePermissions::read_execute, MachineMemoryKind::normal);
    require(static_cast<bool>(mapped));
    // One L2 and one L3 table were allocated beneath the L1 root.
    require(arena.remaining_pages() == 2U);

    auto duplicate = builder.map_page(
        va, pa, MachinePermissions::read_execute, MachineMemoryKind::normal);
    require(!duplicate);
    require(duplicate.error().code == machine_errors::already_mapped);

    auto second = builder.map_page(
        va + 4096ULL,
        pa + 4096ULL,
        MachinePermissions::read_write,
        MachineMemoryKind::normal);
    require(static_cast<bool>(second));
    // Same L1/L2 path: no extra table allocation.
    require(arena.remaining_pages() == 2U);

    auto bad_device_exec = builder.map_page(
        va + 8192ULL,
        0x0000'0000'0900'0000ULL,
        MachinePermissions::read_execute,
        MachineMemoryKind::device);
    require(!bad_device_exec);

    // Inspect the constructed table tree. The leaf must point at the requested
    // PA and be read-only executable at EL1 but never executable at EL0.
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
