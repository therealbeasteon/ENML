#include <array>
#include <cstdlib>
#include <cstdint>

#include <os/kernel/aarch64_kernel_translation_domain.hpp>
#include <os/kernel/aarch64_translation_root_sealer.hpp>

namespace {
template <typename T>
void require(const T& value) { if (!static_cast<bool>(value)) std::abort(); }
}

int main() {
    using namespace os::kernel;
    using namespace os::kernel::aarch64;

    alignas(4096) std::array<std::byte, 20U * 4096U> storage{};
    const auto begin = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(storage.data()));
    EarlyPageArena arena{begin, begin + storage.size()};
    require(arena.valid());

    EarlyStage1Builder user_builder{arena, Stage1Region::lower};
    require(user_builder.initialize());
    auto user_root = TranslationRootSealer::seal(user_builder);
    require(user_root && user_root.value().valid());

    KernelMappingManifest manifest{};
    require(manifest.add({
        .virtual_base = 0x4000'0000ULL,
        .physical_base = 0x0100'0000ULL,
        .length = 2ULL * architectural_page_size,
        .permissions = MachinePermissions::read_execute,
        .kind = MachineMemoryKind::normal,
    }));
    require(manifest.add({
        .virtual_base = 0x0900'0000ULL,
        .physical_base = 0x0900'0000ULL,
        .length = architectural_page_size,
        .permissions = MachinePermissions::read_write,
        .kind = MachineMemoryKind::device,
    }));

    // Reviewed-manifest boundary rejects malformed and dangerous mappings
    // before they reach a page-table builder.
    require(!manifest.add({
        .virtual_base = 0x4000'0001ULL,
        .physical_base = 0x0200'0000ULL,
        .length = architectural_page_size,
        .permissions = MachinePermissions::read,
        .kind = MachineMemoryKind::normal,
    }));
    require(!manifest.add({
        .virtual_base = 0x4100'0000ULL,
        .physical_base = 0x0200'0001ULL,
        .length = architectural_page_size,
        .permissions = MachinePermissions::read,
        .kind = MachineMemoryKind::normal,
    }));
    require(!manifest.add({
        .virtual_base = 0x4200'0000ULL,
        .physical_base = 0x0300'0000ULL,
        .length = architectural_page_size - 1ULL,
        .permissions = MachinePermissions::read,
        .kind = MachineMemoryKind::normal,
    }));
    require(!manifest.add({
        .virtual_base = 0x4300'0000ULL,
        .physical_base = 0x0901'0000ULL,
        .length = architectural_page_size,
        .permissions = MachinePermissions::read_execute,
        .kind = MachineMemoryKind::device,
    }));
    require(!manifest.add({
        .virtual_base = UINT64_MAX - (architectural_page_size - 1ULL),
        .physical_base = 0x0400'0000ULL,
        .length = 2ULL * architectural_page_size,
        .permissions = MachinePermissions::read,
        .kind = MachineMemoryKind::normal,
    }));
    require(!manifest.add({
        .virtual_base = 0x4400'0000ULL,
        .physical_base = 0x0500'0000ULL,
        .length = architectural_page_size,
        .permissions = MachinePermissions::read_execute,
        .kind = MachineMemoryKind::normal,
        .role = KernelMappingRole::guarded_stack,
    }));

    EarlyStage1Builder kernel_builder{arena, Stage1Region::upper};
    require(kernel_builder.initialize());
    require(populate_kernel_translation_builder(manifest, kernel_builder));

    const auto text_alias = kernel_virtual_alias(0x0100'0000ULL);
    const auto text_alias_next = text_alias + architectural_page_size;
    const auto device_alias = kernel_virtual_alias(0x0900'0000ULL);
    auto mapped_text = kernel_builder.mapped(text_alias);
    auto mapped_text_next = kernel_builder.mapped(text_alias_next);
    auto mapped_device = kernel_builder.mapped(device_alias);
    require(mapped_text && mapped_text.value());
    require(mapped_text_next && mapped_text_next.value());
    require(mapped_device && mapped_device.value());

    KernelTranslationDomain domain{};
    require(!domain.establish(kernel_builder));
    require(kernel_builder.seal());
    require(domain.establish(kernel_builder));

    auto plan = prepare_split_translation_plan(user_root.value(), domain, 5U);
    require(plan && plan.value().valid());
    require(plan.value().user_root_physical == user_root.value().root_physical());
    require(plan.value().kernel_root_physical == domain.root_physical());
    require(plan.value().tcr_el1 == split_tcr_el1_for_ips(5U));

    SealedTranslationRoot invalid_user{};
    require(!prepare_split_translation_plan(invalid_user, domain, 5U));
    require(!prepare_split_translation_plan(user_root.value(), domain, 7U));

    EarlyStage1Builder wrong_builder{arena, Stage1Region::lower};
    require(wrong_builder.initialize());
    require(!populate_kernel_translation_builder(manifest, wrong_builder));

    return 0;
}
