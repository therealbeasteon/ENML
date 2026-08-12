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

    alignas(4096) std::array<std::byte, 14U * 4096U> storage{};
    const auto begin = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(storage.data()));
    const auto end = begin + storage.size();

    EarlyPageArena arena{begin, end};
    require(arena.valid());

    EarlyStage1Builder builder{arena};
    auto root = builder.initialize();
    require(static_cast<bool>(root));
    require(root.value() == begin);
    require(builder.lifecycle() == EarlyStage1Builder::Lifecycle::building);

    constexpr std::uint64_t va = 0x0000'0000'4000'0000ULL;
    constexpr std::uint64_t pa = 0x0000'0000'8000'0000ULL;
    auto before = builder.mapped(va);
    require(static_cast<bool>(before));
    require(!before.value());

    auto mapped = builder.map_page(
        va, pa, MachinePermissions::read_execute, MachineMemoryKind::normal);
    require(static_cast<bool>(mapped));

    auto second = builder.map_page(
        va + 4096ULL,
        pa + 4096ULL,
        MachinePermissions::read_write,
        MachineMemoryKind::normal);
    require(static_cast<bool>(second));

    auto removed = builder.unmap_page(va);
    require(static_cast<bool>(removed));
    auto after_remove = builder.mapped(va);
    require(static_cast<bool>(after_remove));
    require(!after_remove.value());

    const auto table_pages_after_remove = builder.remaining_table_pages();
    auto remove_again = builder.unmap_page(va);
    require(!remove_again);
    require(remove_again.error().code == machine_errors::not_mapped);

    auto remapped = builder.map_page(
        va, pa, MachinePermissions::read_execute, MachineMemoryKind::normal);
    require(static_cast<bool>(remapped));
    require(builder.remaining_table_pages() == table_pages_after_remove);

    constexpr std::uint64_t user_va = 0x0000'0000'1000'0000ULL;
    constexpr std::uint64_t user_pa = 0x0000'0000'8200'0000ULL;
    require(static_cast<bool>(builder.map_user_page(
        user_va, user_pa, MachinePermissions::read_execute)));

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
    require((leaf & (3ULL << 6U)) == descriptor::ap_el1_ro_el0_none);
    require((leaf & descriptor::privileged_execute_never) == 0ULL);
    require((leaf & descriptor::unprivileged_execute_never) != 0ULL);

    const auto user_l2_pa = l1[level1_index(user_va)] & page_address_mask;
    auto* user_l2 = reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(user_l2_pa));
    const auto user_l3_pa = user_l2[level2_index(user_va)] & page_address_mask;
    auto* user_l3 = reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(user_l3_pa));
    const auto user_leaf = user_l3[level3_index(user_va)];
    require((user_leaf & page_address_mask) == user_pa);
    require((user_leaf & (3ULL << 6U)) == descriptor::ap_el1_ro_el0_ro);
    require((user_leaf & descriptor::privileged_execute_never) != 0ULL);
    require((user_leaf & descriptor::unprivileged_execute_never) == 0ULL);

    // A process translation root becomes immutable before it is executable.
    require(builder.seal());
    require(builder.executable_process_root());
    require(!builder.map_user_page(
        user_va + architectural_page_size,
        user_pa + architectural_page_size,
        MachinePermissions::read_write));
    require(!builder.unmap_page(user_va));

    // A second process receives a genuinely separate L1 root from the same
    // bounded boot arena; sealing A does not freeze construction of B.
    EarlyStage1Builder other{arena};
    auto other_root = other.initialize();
    require(other_root);
    require(other_root.value() != root.value());
    constexpr std::uint64_t other_user_pa = 0x0000'0000'8300'0000ULL;
    require(other.map_user_page(
        user_va,
        other_user_pa,
        MachinePermissions::read_execute));
    require(other.seal());
    require(other.executable_process_root());

    auto* other_l1 = reinterpret_cast<std::uint64_t*>(
        static_cast<std::uintptr_t>(other_root.value()));
    const auto other_l2_pa = other_l1[level1_index(user_va)] & page_address_mask;
    auto* other_l2 = reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(other_l2_pa));
    const auto other_l3_pa = other_l2[level2_index(user_va)] & page_address_mask;
    auto* other_l3 = reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(other_l3_pa));
    require((other_l3[level3_index(user_va)] & page_address_mask) == other_user_pa);
    require((user_l3[level3_index(user_va)] & page_address_mask) == user_pa);

    // Teardown is an explicit lifecycle transition. Only after begin_retire()
    // can leaf mappings be destructively removed.
    require(builder.begin_retire());
    require(builder.lifecycle() == EarlyStage1Builder::Lifecycle::retiring);
    require(builder.unmap_page(user_va));
    auto retired_user = builder.mapped(user_va);
    require(retired_user && !retired_user.value());

    return 0;
}
