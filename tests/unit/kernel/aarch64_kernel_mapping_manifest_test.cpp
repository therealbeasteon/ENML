#include <os/kernel/aarch64_kernel_mapping_manifest.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;
    using namespace os::kernel::aarch64;

    KernelMappingManifest manifest{};
    require(manifest.add(KernelMappingManifestEntry{
        .virtual_base = 0x4000ULL,
        .physical_base = 0x4000ULL,
        .length = 0x1000ULL,
        .permissions = MachinePermissions::read_execute,
        .kind = MachineMemoryKind::normal,
        .role = KernelMappingRole::ordinary,
    }));
    require(manifest.add(KernelMappingManifestEntry{
        .virtual_base = 0x8000ULL,
        .physical_base = 0x9000ULL,
        .length = 0x2000ULL,
        .permissions = MachinePermissions::read_write,
        .kind = MachineMemoryKind::normal,
        .role = KernelMappingRole::guarded_stack,
    }));
    require(manifest.size() == 2U);

    // A guarded stack manifest entry cannot masquerade as executable/device
    // memory. The manifest represents reviewed EL1 state only.
    require(!manifest.add(KernelMappingManifestEntry{
        .virtual_base = 0xA000ULL,
        .physical_base = 0xB000ULL,
        .length = 0x1000ULL,
        .permissions = MachinePermissions::read_execute,
        .kind = MachineMemoryKind::normal,
        .role = KernelMappingRole::guarded_stack,
    }));
    require(!manifest.add(KernelMappingManifestEntry{}));

    return 0;
}
