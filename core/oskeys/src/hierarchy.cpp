#include <os/keys/hierarchy.hpp>

#include <os/keys/error.hpp>

namespace os::keys {

KeyHierarchy::RootSlot* KeyHierarchy::find_profile(os::core::UserId user) noexcept {
    for (auto& slot : profiles_) {
        if (slot.occupied && slot.binding.owner.user == user) return &slot;
    }
    return nullptr;
}

const KeyHierarchy::RootSlot*
KeyHierarchy::find_profile(os::core::UserId user) const noexcept {
    for (const auto& slot : profiles_) {
        if (slot.occupied && slot.binding.owner.user == user) return &slot;
    }
    return nullptr;
}

KeyHierarchy::RootSlot*
KeyHierarchy::find_application(KeyProtectionBinding binding) noexcept {
    for (auto& slot : applications_) {
        if (slot.occupied && slot.binding == binding) return &slot;
    }
    return nullptr;
}

const KeyHierarchy::RootSlot*
KeyHierarchy::find_application(KeyProtectionBinding binding) const noexcept {
    for (const auto& slot : applications_) {
        if (slot.occupied && slot.binding == binding) return &slot;
    }
    return nullptr;
}

os::core::Result<void>
KeyHierarchy::initialize(KeyProtectionBinding system_binding) noexcept {
    if (provider_ == nullptr || !system_binding.valid() ||
        system_binding.scope != KeyProtectionScope::system) {
        return key_error(errors::invalid_protection_scope);
    }
    if (system_.occupied) {
        if (system_.binding != system_binding) return key_error(errors::hierarchy_conflict);
        return {};
    }
    auto acquired = provider_->acquire_system_root(system_binding);
    if (!acquired) return acquired.error();
    if (!acquired.value().valid()) return key_error(errors::provider_failure);
    system_ = RootSlot{true, system_binding, acquired.value()};
    return {};
}

os::core::Result<void>
KeyHierarchy::ensure_profile(KeyProtectionBinding profile_binding) noexcept {
    if (!system_.occupied) return key_error(errors::hierarchy_not_initialized);
    if (!profile_binding.valid() || profile_binding.scope != KeyProtectionScope::user_profile) {
        return key_error(errors::invalid_protection_scope);
    }
    if (!valid_hierarchy_edge(system_.binding, profile_binding)) return key_error(errors::access_denied);

    if (auto* existing = find_profile(profile_binding.owner.user); existing != nullptr) {
        if (existing->binding != profile_binding) return key_error(errors::hierarchy_conflict);
        return {};
    }

    RootSlot* free_slot = nullptr;
    for (auto& slot : profiles_) if (!slot.occupied) { free_slot = &slot; break; }
    if (free_slot == nullptr) return key_error(errors::hierarchy_capacity);

    auto acquired = provider_->acquire_child_root(system_.reference, system_.binding, profile_binding);
    if (!acquired) return acquired.error();
    if (!acquired.value().valid()) return key_error(errors::provider_failure);
    *free_slot = RootSlot{true, profile_binding, acquired.value()};
    return {};
}

os::core::Result<void>
KeyHierarchy::ensure_application(KeyProtectionBinding application_binding) noexcept {
    if (!system_.occupied) return key_error(errors::hierarchy_not_initialized);
    if (!application_binding.valid() || application_binding.scope != KeyProtectionScope::application) {
        return key_error(errors::invalid_protection_scope);
    }
    if (find_application(application_binding) != nullptr) return {};

    for (const auto& slot : applications_) {
        if (slot.occupied && slot.binding.owner.principal == application_binding.owner.principal &&
            slot.binding.owner.user != application_binding.owner.user) {
            return key_error(errors::hierarchy_conflict);
        }
    }

    auto* profile = find_profile(application_binding.owner.user);
    if (profile == nullptr) return key_error(errors::hierarchy_root_not_found);
    if (!valid_hierarchy_edge(profile->binding, application_binding)) return key_error(errors::access_denied);

    RootSlot* free_slot = nullptr;
    for (auto& slot : applications_) if (!slot.occupied) { free_slot = &slot; break; }
    if (free_slot == nullptr) return key_error(errors::hierarchy_capacity);

    auto acquired = provider_->acquire_child_root(profile->reference, profile->binding, application_binding);
    if (!acquired) return acquired.error();
    if (!acquired.value().valid()) return key_error(errors::provider_failure);
    *free_slot = RootSlot{true, application_binding, acquired.value()};
    return {};
}

os::core::Result<ProviderKeyReference>
KeyHierarchy::generate_application_data_key(
    KeyProtectionBinding application_binding,
    KeyPurpose purpose) noexcept {
    if (!application_binding.valid() || application_binding.scope != KeyProtectionScope::application) {
        return key_error(errors::invalid_protection_scope);
    }
    if (purpose != KeyPurpose::application_data_aead) return key_error(errors::unsupported_purpose);
    const auto* application = find_application(application_binding);
    if (application == nullptr) return key_error(errors::hierarchy_root_not_found);
    auto generated = provider_->generate_under_root(application->reference, application->binding, purpose);
    if (!generated) return generated.error();
    if (!generated.value().valid()) return key_error(errors::provider_failure);
    return generated.value();
}

os::core::Result<ProviderKeyReference>
KeyHierarchy::generate_profile_storage_key(os::core::UserId user) noexcept {
    if (provider_ == nullptr || !system_.occupied) return key_error(errors::hierarchy_not_initialized);
    if (user.value() == 0U) return key_error(errors::invalid_protection_scope);
    const auto* profile = find_profile(user);
    if (profile == nullptr) return key_error(errors::hierarchy_root_not_found);
    auto generated = provider_->generate_under_root(
        profile->reference,
        profile->binding,
        KeyPurpose::profile_storage_aead);
    if (!generated) return generated.error();
    if (!generated.value().valid()) return key_error(errors::provider_failure);
    return generated.value();
}

os::core::Result<ProfileRootErasureReport>
KeyHierarchy::destroy_profile(os::core::UserId user) noexcept {
    if (provider_ == nullptr || !system_.occupied) return key_error(errors::hierarchy_not_initialized);
    RootSlot* profile = find_profile(user);
    if (profile == nullptr) return key_error(errors::hierarchy_root_not_found);

    std::size_t destroyed_children = 0U;
    for (auto& application : applications_) {
        if (!application.occupied || application.binding.owner.user != user) continue;
        auto destroyed = provider_->destroy_root(application.reference, application.binding);
        if (!destroyed) return destroyed.error();
        application = RootSlot{};
        ++destroyed_children;
    }

    auto destroyed_profile = provider_->destroy_root(profile->reference, profile->binding);
    if (!destroyed_profile) return destroyed_profile.error();
    *profile = RootSlot{};
    return ProfileRootErasureReport{
        .application_roots_destroyed = destroyed_children,
        .assurance = provider_->root_erasure_assurance(),
    };
}

std::size_t KeyHierarchy::profile_count() const noexcept {
    std::size_t count = 0U;
    for (const auto& slot : profiles_) if (slot.occupied) ++count;
    return count;
}

std::size_t KeyHierarchy::application_count() const noexcept {
    std::size_t count = 0U;
    for (const auto& slot : applications_) if (slot.occupied) ++count;
    return count;
}

} // namespace os::keys
