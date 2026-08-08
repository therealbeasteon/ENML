#pragma once

#include <array>
#include <cstddef>

#include <os/core/result.hpp>
#include <os/package/package.hpp>

namespace os::package {

// Milestone-only bounded storage. These are not public product limits.
inline constexpr std::size_t m1_registry_application_capacity = 32U;
inline constexpr std::size_t m1_registry_generations_per_application = 4U;

class PackageRegistry final {
public:
    PackageRegistry() noexcept = default;

    // Staging never changes the active generation. A textual PackageId is
    // reserved to one signer lineage.
    [[nodiscard]] os::core::Result<void>
    stage_generation(const PackageGenerationRecord& record) noexcept;

    [[nodiscard]] os::core::Result<void>
    activate(
        const ApplicationIdentity& application,
        PackageGenerationId generation) noexcept;

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
