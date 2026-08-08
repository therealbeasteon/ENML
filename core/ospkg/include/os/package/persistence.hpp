#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/package/registry.hpp>

namespace os::package {

inline constexpr std::uint32_t package_registry_snapshot_magic_v1 = 0x31525045U; // "EPR1" LE
inline constexpr std::uint16_t package_registry_snapshot_version_v1 = 1U;
inline constexpr std::uint16_t package_registry_snapshot_header_size_v1 = 24U;
inline constexpr std::size_t max_package_registry_snapshot_bytes = 16U * 1024U;

namespace persistence_errors {
inline constexpr std::uint32_t invalid_state_directory = 200U;
inline constexpr std::uint32_t io_failure = 201U;
inline constexpr std::uint32_t snapshot_too_large = 202U;
inline constexpr std::uint32_t malformed_snapshot = 203U;
inline constexpr std::uint32_t unsupported_snapshot_version = 204U;
inline constexpr std::uint32_t snapshot_inconsistent = 205U;
} // namespace persistence_errors

// Internal M1 package-state durability layer. The caller supplies an already
// authorized directory handle; this class never accepts an application path.
class PersistentPackageRegistry final {
public:
    PersistentPackageRegistry(const PersistentPackageRegistry&) = delete;
    PersistentPackageRegistry& operator=(const PersistentPackageRegistry&) = delete;
    PersistentPackageRegistry(PersistentPackageRegistry&&) noexcept = default;
    PersistentPackageRegistry& operator=(PersistentPackageRegistry&&) noexcept = default;
    ~PersistentPackageRegistry() = default;

    [[nodiscard]] static os::core::Result<PersistentPackageRegistry>
    open(os::core::NativeHandle state_directory) noexcept;

    // A mutation becomes visible in memory only after the replacement snapshot
    // has been fsynced, atomically renamed, and the directory fsynced.
    [[nodiscard]] os::core::Result<void>
    stage_generation(const PackageGenerationRecord& record) noexcept;

    [[nodiscard]] os::core::Result<void>
    activate(
        const ApplicationIdentity& application,
        PackageGenerationId generation) noexcept;

    [[nodiscard]] os::core::Result<PackageGenerationRecord>
    active(const ApplicationIdentity& application) const noexcept {
        return registry_.active(application);
    }

    [[nodiscard]] os::core::Result<PackageGenerationRecord>
    generation(
        const ApplicationIdentity& application,
        PackageGenerationId generation) const noexcept {
        return registry_.generation(application, generation);
    }

    [[nodiscard]] os::core::Result<ApplicationIdentity>
    owner(const PackageId& package_id) const noexcept {
        return registry_.owner(package_id);
    }

    [[nodiscard]] std::size_t application_count() const noexcept {
        return registry_.application_count();
    }

    [[nodiscard]] const PackageRegistry& registry() const noexcept { return registry_; }

private:
    PersistentPackageRegistry(os::core::NativeHandle state_directory, PackageRegistry registry) noexcept
        : state_directory_(static_cast<os::core::NativeHandle&&>(state_directory)), registry_(registry) {}

    [[nodiscard]] static os::core::Result<PackageRegistry>
    load_snapshot(int directory_fd) noexcept;

    [[nodiscard]] os::core::Result<void>
    persist_candidate(const PackageRegistry& candidate) noexcept;

    os::core::NativeHandle state_directory_ {};
    PackageRegistry registry_ {};
};

} // namespace os::package
