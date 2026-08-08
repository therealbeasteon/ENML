#pragma once

#include <array>
#include <cstddef>

#include <os/core/result.hpp>
#include <os/package/package.hpp>

namespace os::package {

// Milestone-only bounded storage. These are not public product limits.
inline constexpr std::size_t m1_registry_application_capacity = 32U;
inline constexpr std::size_t m1_registry_generations_per_application = 4U;

class PersistentPackageRegistry;

class PackageRegistry final {
public:
    PackageRegistry() noexcept = default;

    // Staging never changes the active generation. A textual PackageId is
    // permanently reserved to the recorded signer lineage in this registry,
    // including after uninstall.
    [[nodiscard]] os::core::Result<void>
    stage_generation(const PackageGenerationRecord& record) noexcept;

    // Activation affects future launch resolution only. Existing processes are
    // generation-bound by App Manager and are not rewritten by this mutation.
    [[nodiscard]] os::core::Result<void>
    activate(
        const ApplicationIdentity& application,
        PackageGenerationId generation) noexcept;

    // M1.5 uninstall is a durable launch revocation, not identity/data erasure.
    // It clears the active generation while retaining signer ownership and
    // generation metadata so a different signer cannot claim the PackageId and
    // monotonic update history is not silently reset.
    [[nodiscard]] os::core::Result<void>
    uninstall(const ApplicationIdentity& application) noexcept;

    [[nodiscard]] os::core::Result<PackageGenerationRecord>
    active(const ApplicationIdentity& application) const noexcept;

    [[nodiscard]] os::core::Result<PackageGenerationRecord>
    generation(
        const ApplicationIdentity& application,
        PackageGenerationId generation) const noexcept;

    [[nodiscard]] os::core::Result<ApplicationIdentity>
    owner(const PackageId& package_id) const noexcept;

    [[nodiscard]] std::size_t application_count() const noexcept;

private:
    friend class PersistentPackageRegistry;

    struct Slot final {
        bool occupied {false};
        ApplicationIdentity application {};
        std::array<PackageGenerationRecord, m1_registry_generations_per_application> generations {};
        std::size_t generation_count {0U};
        bool has_active {false};
        PackageGenerationId active_generation {};
    };

    std::array<Slot, m1_registry_application_capacity> slots_ {};

    [[nodiscard]] Slot* find_by_package_id(const PackageId& package_id) noexcept;
    [[nodiscard]] const Slot* find_by_package_id(const PackageId& package_id) const noexcept;
};

} // namespace os::package
