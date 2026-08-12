#include <os/kernel/aarch64_kernel_mapping_manifest.hpp>
#include <os/kernel/aarch64_kernel_translation_domain.hpp>
#include <os/kernel/aarch64_translation.hpp>

#include <array>
#include <cstdlib>

namespace {
template <typename T>
void require(const T& value) {
    if (!static_cast<bool>(value)) std::abort();
}
}

int main() {
    using namespace os::kernel;
    using namespace os::kernel::aarch64;

    require(stage1_t0sz == 25U);
    require(stage1_t1sz == 25U);
    require(user_virtual_limit == (1ULL << 39U));
    require(kernel_virtual_base == 0xFFFF'FF80'0000'0000ULL);
    require(stage1_regions_disjoint());

    require(stage1_virtual_address((1ULL << 39U) - 1ULL));
    require(user_stage1_virtual_address((1ULL << 39U) - 1ULL));
    require(!stage1_virtual_address(1ULL << 39U));
    require(!user_stage1_virtual_address(kernel_virtual_base));
    require(kernel_stage1_virtual_address(kernel_virtual_base));
    require(kernel_stage1_virtual_address(UINT64_MAX));
    require(!kernel_stage1_virtual_address(kernel_virtual_base - 1ULL));
    require(!kernel_stage1_virtual_address(0ULL));

    // The future TTBR1 map is projected from the one reviewed physical manifest,
    // preserving attributes while replacing temporary low aliases with a
    // deterministic upper-canonical alias.
    KernelMappingManifest manifest{};
    require(manifest.add({
        .virtual_base = 0x0000'0000'0040'0000ULL,
        .physical_base = 0x0000'0000'0040'0000ULL,
        .length = architectural_page_size,
        .permissions = MachinePermissions::read_execute,
        .kind = MachineMemoryKind::normal,
    }));
    require(manifest.add({
        .virtual_base = 0x0000'0000'0900'0000ULL,
        .physical_base = 0x0000'0000'0900'0000ULL,
        .length = architectural_page_size,
        .permissions = MachinePermissions::read_write,
        .kind = MachineMemoryKind::device,
    }));
    auto projected = project_kernel_manifest_to_upper(manifest);
    require(projected);
    require(projected.value().size() == manifest.size());
    require(projected.value()[0].virtual_base ==
            kernel_virtual_alias(manifest[0].physical_base));
    require(projected.value()[0].physical_base == manifest[0].physical_base);
    require(projected.value()[0].permissions == manifest[0].permissions);
    require(projected.value()[0].kind == manifest[0].kind);
    require(kernel_stage1_virtual_address(projected.value()[0].virtual_base));
    require(projected.value()[1].virtual_base ==
            kernel_virtual_alias(manifest[1].physical_base));
    require(projected.value()[1].kind == MachineMemoryKind::device);

    KernelMappingManifest outside_ttbr1_span{};
    require(outside_ttbr1_span.add({
        .virtual_base = 0x1000ULL,
        .physical_base = user_virtual_limit,
        .length = architectural_page_size,
        .permissions = MachinePermissions::read_write,
        .kind = MachineMemoryKind::normal,
    }));
    require(!project_kernel_manifest_to_upper(outside_ttbr1_span));
    require(kernel_virtual_alias(user_virtual_limit) == 0ULL);

    // Kernel translation authority cannot be minted from a raw address or a
    // process/lower root. It requires a sealed upper-region page-table builder.
    alignas(4096) std::array<std::byte, 8U * 4096U> domain_storage{};
    const auto domain_begin = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(domain_storage.data()));
    EarlyPageArena domain_arena{domain_begin, domain_begin + domain_storage.size()};

    EarlyStage1Builder lower_builder{domain_arena};
    require(lower_builder.initialize());
    require(lower_builder.seal());
    require(lower_builder.executable_process_root());
    require(!lower_builder.sealed_kernel_root());

    KernelTranslationDomain kernel_domain{};
    require(!kernel_domain.established());
    require(!kernel_domain.establish(lower_builder));
    require(!kernel_domain.established());

    EarlyStage1Builder upper_builder{domain_arena, Stage1Region::upper};
    auto upper_root = upper_builder.initialize();
    require(upper_root);
    require(!upper_builder.executable_process_root());
    require(!upper_builder.sealed_kernel_root());
    require(upper_builder.seal());
    require(upper_builder.sealed_kernel_root());
    require(kernel_domain.establish(upper_builder));
    require(kernel_domain.established());
    require(kernel_domain.root_physical() == upper_root.value());

    EarlyStage1Builder other_upper{domain_arena, Stage1Region::upper};
    require(other_upper.initialize());
    require(other_upper.seal());
    require(!kernel_domain.establish(other_upper));
    require(kernel_domain.root_physical() == upper_root.value());

    require(page_aligned(0x4000ULL));
    require(!page_aligned(0x4001ULL));

    require(level1_index(0x0000'0001'8000'0000ULL) == 6U);
    require(level2_index(0x0000'0001'8020'0000ULL) == 1U);
    require(level3_index(0x0000'0001'8020'3000ULL) == 3U);

    const auto table = table_descriptor(0x0000'0000'0040'0000ULL);
    require((table & descriptor::valid) != 0ULL);
    require((table & descriptor::table_or_page) != 0ULL);
    require(table_descriptor(0x4001ULL) == 0ULL);

    const auto rw = page_descriptor(
        0x0000'0000'0080'0000ULL,
        MachinePermissions::read_write,
        MachineMemoryKind::normal);
    require(rw != 0ULL);
    require((rw & (3ULL << 6U)) == descriptor::ap_el1_rw_el0_none);
    require((rw & descriptor::privileged_execute_never) != 0ULL);
    require((rw & descriptor::unprivileged_execute_never) != 0ULL);
    require((rw & descriptor::access_flag) != 0ULL);

    const auto rx = page_descriptor(
        0x0000'0000'0080'1000ULL,
        MachinePermissions::read_execute,
        MachineMemoryKind::normal);
    require(rx != 0ULL);
    require((rx & (3ULL << 6U)) == descriptor::ap_el1_ro_el0_none);
    require((rx & descriptor::privileged_execute_never) == 0ULL);
    require((rx & descriptor::unprivileged_execute_never) != 0ULL);

    const auto user_rw = user_page_descriptor(
        0x0000'0000'0080'2000ULL,
        MachinePermissions::read_write);
    require(user_rw != 0ULL);
    require((user_rw & (3ULL << 6U)) == descriptor::ap_el1_rw_el0_rw);
    require((user_rw & descriptor::privileged_execute_never) != 0ULL);
    require((user_rw & descriptor::unprivileged_execute_never) != 0ULL);

    const auto user_rx = user_page_descriptor(
        0x0000'0000'0080'3000ULL,
        MachinePermissions::read_execute);
    require(user_rx != 0ULL);
    require((user_rx & (3ULL << 6U)) == descriptor::ap_el1_ro_el0_ro);
    require((user_rx & descriptor::privileged_execute_never) != 0ULL);
    require((user_rx & descriptor::unprivileged_execute_never) == 0ULL);

    const auto user_ro = user_page_descriptor(
        0x0000'0000'0080'4000ULL,
        MachinePermissions::read);
    require(user_ro != 0ULL);
    require((user_ro & (3ULL << 6U)) == descriptor::ap_el1_ro_el0_ro);
    require((user_ro & descriptor::privileged_execute_never) != 0ULL);
    require((user_ro & descriptor::unprivileged_execute_never) != 0ULL);

    const auto dev = page_descriptor(
        0x0000'0000'0900'0000ULL,
        MachinePermissions::read_write,
        MachineMemoryKind::device);
    require(dev != 0ULL);
    require((dev & descriptor::privileged_execute_never) != 0ULL);
    require(page_descriptor(
        0x0000'0000'0900'0000ULL,
        MachinePermissions::read_execute,
        MachineMemoryKind::device) == 0ULL);

    require(cookie_ips(0U) == 0U);
    require(cookie_ips(5U) == 5U);
    require(cookie_ips(6U) == 5U);

    const auto bringup_tcr = tcr_el1_for_ips(5U);
    require(bringup_tcr != 0ULL);
    require((bringup_tcr & 0x3FULL) == stage1_t0sz);
    require((bringup_tcr & (1ULL << 23U)) != 0ULL);
    require(tcr_el1_for_ips(6U) == 0ULL);

    const auto split_tcr = split_tcr_el1_for_ips(5U);
    require(split_tcr != 0ULL);
    require((split_tcr & 0x3FULL) == stage1_t0sz);
    require(((split_tcr >> 16U) & 0x3FULL) == stage1_t1sz);
    require(((split_tcr >> 8U) & 0x3ULL) == 1ULL);
    require(((split_tcr >> 10U) & 0x3ULL) == 1ULL);
    require(((split_tcr >> 12U) & 0x3ULL) == 3ULL);
    require((split_tcr & (1ULL << 22U)) == 0ULL);
    require((split_tcr & (1ULL << 23U)) == 0ULL);
    require(((split_tcr >> 24U) & 0x3ULL) == 1ULL);
    require(((split_tcr >> 26U) & 0x3ULL) == 1ULL);
    require(((split_tcr >> 28U) & 0x3ULL) == 3ULL);
    require(((split_tcr >> 30U) & 0x3ULL) == 2ULL);
    require(((split_tcr >> 32U) & 0x7ULL) == 5ULL);
    require(split_tcr_el1_for_ips(6U) == 0ULL);

    return 0;
}
