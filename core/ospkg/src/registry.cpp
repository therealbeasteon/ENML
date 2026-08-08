#include <os/package/registry.hpp>

#include <cstdint>

#include <os/core/error.hpp>

namespace os::package {
namespace {

[[nodiscard]] constexpr os::core::Error package_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::package, code);
}

} // namespace

PackageRegistry::Slot* PackageRegistry::find_by_package_id(const PackageId& package_id) noexcept {
    for (auto& slot : slots_) {
        if (slot.occupied && slot.application.package_id == package_id) return &slot;
    }
    return nullptr;
}

const PackageRegistry::Slot* PackageRegistry::find_by_package_id(const PackageId& package_id) const noexcept {
    for (const auto& slot : slots_) {
        if (slot.occupied && slot.application.package_id == package_id) return &slot;
    }
    return nullptr;
}

os::core::Result<void>
PackageRegistry::stage_generation(const PackageGenerationRecord& record) noexcept {
    if (!record.valid()) {
        if (!record.application.package_id.valid()) return package_error(errors::invalid_package_id);
        if (!record.application.signer_lineage.valid()) return package_error(errors::invalid_signer_lineage);
        if (!record.content.valid()) return package_error(errors::invalid_content_digest);
        return package_error(errors::invalid_generation);
    }

    Slot* slot = find_by_package_id(record.application.package_id);
    if (slot == nullptr) {
        for (auto& candidate : slots_) {
            if (!candidate.occupied) {
                slot = &candidate;
                break;
            }
        }
        if (slot == nullptr) return package_error(errors::registry_full);
        slot->occupied = true;
        slot->application = record.application;
    } else if (slot->application.signer_lineage != record.application.signer_lineage) {
        return package_error(errors::package_id_collision);
    }

    if (slot->generation_count != 0U) {
        const auto previous = slot->generations[slot->generation_count - 1U].generation.value();
        if (record.generation.value() <= previous) return package_error(errors::generation_conflict);
    }

    if (slot->generation_count >= slot->generations.size()) {
        return package_error(errors::generation_capacity);
    }

    slot->generations[slot->generation_count++] = record;
    return {};
}

os::core::Result<void>
PackageRegistry::activate(
    const ApplicationIdentity& application,
    PackageGenerationId generation_id) noexcept {
    if (!application.valid() || generation_id.value() == 0U) return package_error(errors::invalid_generation);

    Slot* slot = find_by_package_id(application.package_id);
    if (slot == nullptr) return package_error(errors::unknown_package);
    if (slot->application.signer_lineage != application.signer_lineage) {
        return package_error(errors::package_id_collision);
    }

    for (std::size_t index = 0U; index < slot->generation_count; ++index) {
        if (slot->generations[index].generation == generation_id) {
            slot->active_generation = generation_id;
            slot->has_active = true;
            return {};
        }
    }
    return package_error(errors::unknown_generation);
}

os::core::Result<PackageGenerationRecord>
PackageRegistry::active(const ApplicationIdentity& application) const noexcept {
    if (!application.valid()) return package_error(errors::unknown_package);
    const Slot* slot = find_by_package_id(application.package_id);
    if (slot == nullptr) return package_error(errors::unknown_package);
    if (slot->application.signer_lineage != application.signer_lineage) {
        return package_error(errors::package_id_collision);
    }
    if (!slot->has_active) return package_error(errors::no_active_generation);
    return generation(application, slot->active_generation);
}

os::core::Result<PackageGenerationRecord>
PackageRegistry::generation(
    const ApplicationIdentity& application,
    PackageGenerationId generation_id) const noexcept {
    if (!application.valid() || generation_id.value() == 0U) return package_error(errors::unknown_generation);
    const Slot* slot = find_by_package_id(application.package_id);
    if (slot == nullptr) return package_error(errors::unknown_package);
    if (slot->application.signer_lineage != application.signer_lineage) {
        return package_error(errors::package_id_collision);
    }
    for (std::size_t index = 0U; index < slot->generation_count; ++index) {
        if (slot->generations[index].generation == generation_id) return slot->generations[index];
    }
    return package_error(errors::unknown_generation);
}

os::core::Result<ApplicationIdentity>
PackageRegistry::owner(const PackageId& package_id) const noexcept {
    if (!package_id.valid()) return package_error(errors::invalid_package_id);
    const Slot* slot = find_by_package_id(package_id);
    if (slot == nullptr) return package_error(errors::unknown_package);
    return slot->application;
}

std::size_t PackageRegistry::application_count() const noexcept {
    std::size_t count = 0U;
    for (const auto& slot : slots_) {
        if (slot.occupied) ++count;
    }
    return count;
}

} // namespace os::package
