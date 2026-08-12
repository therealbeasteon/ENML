#include <os/storage/protected_namespace.hpp>

#include <limits>

namespace os::storage {

const ProtectedNamespaceEntry*
ProtectedNamespaceRegistry::find(
    os::core::PrincipalId principal,
    os::core::UserId user,
    const RelativePath& path) const noexcept {
    for (const auto& slot : slots_) {
        if (!slot.occupied) continue;
        if (slot.entry.principal == principal && slot.entry.user == user && slot.entry.path == path) {
            return &slot.entry;
        }
    }
    return nullptr;
}

ProtectedNamespaceEntry*
ProtectedNamespaceRegistry::find(
    os::core::PrincipalId principal,
    os::core::UserId user,
    const RelativePath& path) noexcept {
    for (auto& slot : slots_) {
        if (!slot.occupied) continue;
        if (slot.entry.principal == principal && slot.entry.user == user && slot.entry.path == path) {
            return &slot.entry;
        }
    }
    return nullptr;
}

os::core::Result<ProtectedNamespaceEntry>
ProtectedNamespaceRegistry::create(
    os::core::PrincipalId principal,
    os::core::UserId user,
    const RelativePath& path) noexcept {
    if (ids_ == nullptr || !os::core::valid_principal(principal) || user.value() == 0U || !path.valid()) {
        return protected_namespace_error(protected_namespace_errors::invalid_identity);
    }
    if (find(principal, user, path) != nullptr) {
        return protected_namespace_error(protected_namespace_errors::already_exists);
    }

    Slot* free_slot = nullptr;
    for (auto& slot : slots_) {
        if (!slot.occupied) {
            free_slot = &slot;
            break;
        }
    }
    if (free_slot == nullptr) {
        return protected_namespace_error(protected_namespace_errors::capacity);
    }

    auto object_id = ids_->next();
    if (!object_id) return object_id.error();
    if (!object_id.value().valid()) {
        return protected_namespace_error(protected_namespace_errors::invalid_identity);
    }

    free_slot->occupied = true;
    free_slot->entry = ProtectedNamespaceEntry{
        .principal = principal,
        .user = user,
        .path = path,
        .object_id = object_id.value(),
        .generation = 1U,
    };
    return free_slot->entry;
}

os::core::Result<ProtectedObjectVersion>
ProtectedNamespaceRegistry::propose_next_generation(
    os::core::PrincipalId principal,
    os::core::UserId user,
    const RelativePath& path,
    std::uint64_t expected_generation) const noexcept {
    const auto* entry = find(principal, user, path);
    if (entry == nullptr) {
        return protected_namespace_error(protected_namespace_errors::not_found);
    }
    if (expected_generation == 0U || entry->generation != expected_generation ||
        entry->generation == std::numeric_limits<std::uint64_t>::max()) {
        return protected_namespace_error(protected_namespace_errors::generation_conflict);
    }
    return ProtectedObjectVersion{
        .user = entry->user,
        .object_id = entry->object_id,
        .generation = entry->generation + 1U,
    };
}

os::core::Result<void>
ProtectedNamespaceRegistry::publish_generation(
    os::core::PrincipalId principal,
    os::core::UserId user,
    const RelativePath& path,
    std::uint64_t expected_generation,
    std::uint64_t new_generation) noexcept {
    auto* entry = find(principal, user, path);
    if (entry == nullptr) {
        return protected_namespace_error(protected_namespace_errors::not_found);
    }
    if (expected_generation == 0U || new_generation == 0U ||
        entry->generation != expected_generation || new_generation <= expected_generation) {
        return protected_namespace_error(protected_namespace_errors::generation_conflict);
    }
    entry->generation = new_generation;
    return {};
}

std::size_t ProtectedNamespaceRegistry::size() const noexcept {
    std::size_t count = 0U;
    for (const auto& slot : slots_) {
        if (slot.occupied) ++count;
    }
    return count;
}

} // namespace os::storage
