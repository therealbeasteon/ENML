#include <os/keys/testing/openssl_provider.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>

#include <os/keys/error.hpp>

namespace os::keys::testing {

RootKeyReference OpenSslTestKeyProvider::make_root_reference(
    std::size_t index,
    std::uint32_t generation) noexcept {
    const auto token = static_cast<std::uint64_t>(index + 1U);
    return RootKeyReference{(static_cast<std::uint64_t>(generation) << 32U) | token};
}

OpenSslTestKeyProvider::RootSlot*
OpenSslTestKeyProvider::resolve_root(RootKeyReference reference) noexcept {
    if (!reference.valid()) return nullptr;
    const std::uint32_t token = static_cast<std::uint32_t>(reference.value & 0xFFFFFFFFULL);
    const std::uint32_t generation = static_cast<std::uint32_t>(reference.value >> 32U);
    if (token == 0U || generation == 0U || static_cast<std::size_t>(token) > root_slots_.size()) return nullptr;
    auto& slot = root_slots_[static_cast<std::size_t>(token - 1U)];
    if (!slot.occupied || slot.generation != generation) return nullptr;
    return &slot;
}

const OpenSslTestKeyProvider::RootSlot*
OpenSslTestKeyProvider::resolve_root(RootKeyReference reference) const noexcept {
    if (!reference.valid()) return nullptr;
    const std::uint32_t token = static_cast<std::uint32_t>(reference.value & 0xFFFFFFFFULL);
    const std::uint32_t generation = static_cast<std::uint32_t>(reference.value >> 32U);
    if (token == 0U || generation == 0U || static_cast<std::size_t>(token) > root_slots_.size()) return nullptr;
    const auto& slot = root_slots_[static_cast<std::size_t>(token - 1U)];
    if (!slot.occupied || slot.generation != generation) return nullptr;
    return &slot;
}

os::core::Result<RootKeyReference>
OpenSslTestKeyProvider::install_root(KeyProtectionBinding binding, RootKeyReference parent) noexcept {
    if (!binding.valid()) return key_error(errors::invalid_protection_scope);
    for (std::size_t index = 0U; index < root_slots_.size(); ++index) {
        auto& slot = root_slots_[index];
        if (slot.occupied || slot.generation == std::numeric_limits<std::uint32_t>::max()) continue;
        ++slot.generation;
        slot.occupied = true;
        slot.binding = binding;
        slot.parent = parent;
        return make_root_reference(index, slot.generation);
    }
    return key_error(errors::hierarchy_capacity);
}

os::core::Result<RootKeyReference>
OpenSslTestKeyProvider::acquire_system_root(KeyProtectionBinding system_binding) noexcept {
    if (!system_binding.valid() || system_binding.scope != KeyProtectionScope::system) {
        return key_error(errors::invalid_protection_scope);
    }
    for (std::size_t index = 0U; index < root_slots_.size(); ++index) {
        const auto& slot = root_slots_[index];
        if (!slot.occupied || slot.binding.scope != KeyProtectionScope::system) continue;
        if (slot.binding != system_binding || slot.parent.valid()) return key_error(errors::hierarchy_conflict);
        return make_root_reference(index, slot.generation);
    }
    return install_root(system_binding, {});
}

os::core::Result<RootKeyReference>
OpenSslTestKeyProvider::acquire_child_root(
    RootKeyReference parent,
    KeyProtectionBinding parent_binding,
    KeyProtectionBinding child_binding) noexcept {
    const auto* parent_slot = resolve_root(parent);
    if (parent_slot == nullptr || parent_slot->binding != parent_binding) return key_error(errors::access_denied);
    if (!valid_hierarchy_edge(parent_binding, child_binding)) return key_error(errors::access_denied);
    for (std::size_t index = 0U; index < root_slots_.size(); ++index) {
        const auto& slot = root_slots_[index];
        if (!slot.occupied || slot.binding != child_binding) continue;
        if (slot.parent != parent) return key_error(errors::hierarchy_conflict);
        return make_root_reference(index, slot.generation);
    }
    return install_root(child_binding, parent);
}

os::core::Result<ProviderKeyReference>
OpenSslTestKeyProvider::generate_under_root(
    RootKeyReference root,
    KeyProtectionBinding binding,
    KeyPurpose purpose) noexcept {
    const auto* slot = resolve_root(root);
    if (slot == nullptr || slot->binding != binding) return key_error(errors::access_denied);

    const bool application_ok =
        binding.scope == KeyProtectionScope::application &&
        purpose == KeyPurpose::application_data_aead;
    const bool profile_storage_ok =
        binding.scope == KeyProtectionScope::user_profile &&
        (purpose == KeyPurpose::profile_storage_aead ||
         purpose == KeyPurpose::profile_storage_metadata_aead);
    if (!application_ok && !profile_storage_ok) return key_error(errors::access_denied);
    // The scope/purpose pairing above is this path's admission rule, and it
    // has just been applied. Calling generate() here would re-check against
    // the flat path's rule, which is application-only by design and rejected
    // every profile storage key this function had already authorised.
    return generate_material();
}

os::core::Result<void>
OpenSslTestKeyProvider::destroy_root(RootKeyReference root, KeyProtectionBinding binding) noexcept {
    auto* slot = resolve_root(root);
    if (slot == nullptr || slot->binding != binding) return key_error(errors::access_denied);
    for (const auto& child : root_slots_) {
        if (child.occupied && child.parent == root) return key_error(errors::access_denied);
    }
    slot->occupied = false;
    slot->binding = {};
    slot->parent = {};
    return {};
}

} // namespace os::keys::testing
