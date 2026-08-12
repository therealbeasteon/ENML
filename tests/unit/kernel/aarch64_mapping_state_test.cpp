#include <os/kernel/aarch64_mapping_state.hpp>

#include <array>
#include <cstdlib>
#include <cstdint>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;
    using namespace os::kernel::aarch64;

    alignas(4096) std::array<std::byte, 12U * 4096U> tables_a{};
    alignas(4096) std::array<std::byte, 12U * 4096U> tables_b{};
    const auto a_begin = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(tables_a.data()));
    const auto b_begin = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(tables_b.data()));

    EarlyPageArena arena_a{a_begin, a_begin + tables_a.size()};
    EarlyPageArena arena_b{b_begin, b_begin + tables_b.size()};
    EarlyStage1Builder builder_a{arena_a};
    EarlyStage1Builder builder_b{arena_b};
    require(static_cast<bool>(builder_a.initialize()));
    require(static_cast<bool>(builder_b.initialize()));

    NativePhysicalLedger ledger{};
    NativeAddressSpaceState space_a{};
    NativeAddressSpaceState space_b{};
    require(static_cast<bool>(space_a.bind(ledger, builder_a)));
    require(static_cast<bool>(space_b.bind(ledger, builder_b)));

    constexpr std::uint64_t physical = 0x0000'0000'8000'0000ULL;
    constexpr std::uint64_t va_a = 0x0000'0000'4000'0000ULL;
    constexpr std::uint64_t va_b = 0x0000'0000'5000'0000ULL;

    require(static_cast<bool>(space_a.map(
        va_a, physical, 4096ULL,
        MachinePermissions::read_write, MachineMemoryKind::normal)));
    require(space_a.mapping_count() == 1U);
    require(ledger.occupied == 1U);

    auto executable_alias = space_b.map(
        va_b, physical, 4096ULL,
        MachinePermissions::read_execute, MachineMemoryKind::normal);
    require(!executable_alias);
    require(executable_alias.error().code == machine_errors::writable_executable_alias);

    auto early_retire = space_a.retire_unmapped(va_a, 4096ULL);
    require(!early_retire);
    require(early_retire.error().code == machine_errors::mapping_ledger_inconsistent);
    require(ledger.occupied == 1U);

    require(static_cast<bool>(builder_a.unmap_page(va_a)));
    require(static_cast<bool>(space_a.retire_unmapped(va_a, 4096ULL)));
    require(space_a.mapping_count() == 0U);
    require(ledger.occupied == 0U);

    require(static_cast<bool>(space_b.map(
        va_b, physical, 4096ULL,
        MachinePermissions::read_execute, MachineMemoryKind::normal)));
    require(ledger.occupied == 1U);

    constexpr std::uint64_t user_code_va = 0x0000'0000'1000'0000ULL;
    constexpr std::uint64_t user_code_pa = 0x0000'0000'8200'0000ULL;
    require(static_cast<bool>(space_a.map_user(
        user_code_va, user_code_pa, 4096ULL,
        MachinePermissions::read_execute)));
    auto user_code = space_a.exact_mapping(user_code_va, 4096ULL);
    require(static_cast<bool>(user_code));
    require(user_code.value().user_accessible);
    require(!user_code.value().kernel_stack);
    require(user_code.value().kind == MachineMemoryKind::normal);

    // A writable alias of user executable code is forbidden globally just as
    // it is for kernel executable code.
    auto writable_user_alias = space_b.map_user(
        user_code_va + 0x200000ULL, user_code_pa, 4096ULL,
        MachinePermissions::read_write);
    require(!writable_user_alias);
    require(writable_user_alias.error().code == machine_errors::writable_executable_alias);

    constexpr std::uint64_t user_stack_va = 0x0000'0000'1001'0000ULL;
    constexpr std::uint64_t user_stack_pa = 0x0000'0000'8201'0000ULL;
    require(static_cast<bool>(space_a.map_user(
        user_stack_va, user_stack_pa, 4096ULL,
        MachinePermissions::read_write)));
    auto user_stack = space_a.exact_mapping(user_stack_va, 4096ULL);
    require(static_cast<bool>(user_stack));
    require(user_stack.value().user_accessible);
    require(user_stack.value().permissions == MachinePermissions::read_write);

    constexpr std::uint64_t stack_va = 0x0000'0000'6000'1000ULL;
    constexpr std::uint64_t stack_pa = 0x0000'0000'8100'0000ULL;
    require(static_cast<bool>(space_a.map_kernel_stack(
        stack_va, stack_pa, 2ULL * 4096ULL)));
    require(space_a.valid_kernel_stack_top(stack_va + 2ULL * 4096ULL));
    require(!space_a.valid_kernel_stack_top(stack_va + 4096ULL));

    auto guard = builder_a.mapped(stack_va - 4096ULL);
    require(static_cast<bool>(guard));
    require(!guard.value());

    return 0;
}
