#include <os/keys/product_store.hpp>

#include <os/keys/control.hpp>
#include <os/keys/error.hpp>

namespace os::keys {

os::core::Result<void>
HierarchicalPolicyKeyStore::authorize(KeyOwner owner) const noexcept {
    if (persistent_ == nullptr || hierarchy_ == nullptr || policy_ == nullptr) {
        return key_error(errors::provider_failure);
    }
    if (!policy_->enabled(owner)) {
        return key_error(errors::policy_not_registered);
    }
    return {};
}

os::core::Result<KeyDescriptor>
HierarchicalPolicyKeyStore::create(
    KeyOwner owner,
    KeyId id,
    KeyPurpose purpose,
    RightsMask rights) noexcept {
    auto allowed = authorize(owner);
    if (!allowed) return allowed.error();

    const auto binding = application_key_binding(owner.principal, owner.user);
    auto generated = hierarchy_->generate_application_data_key(binding, purpose);
    if (!generated) return generated.error();

    // PersistentKeyRegistry takes ownership of the generated reference whether
    // publication succeeds or fails, including the rename-vs-directory-fsync
    // edge handled by M2.6.
    return persistent_->adopt_generated(owner, id, purpose, rights, generated.value());
}

os::core::Result<KeyDescriptor>
HierarchicalPolicyKeyStore::rotate(KeyOwner caller, KeyId id) noexcept {
    auto allowed = authorize(caller);
    if (!allowed) return allowed.error();

    auto descriptor = persistent_->describe(caller, id);
    if (!descriptor) return descriptor.error();

    const auto binding = application_key_binding(caller.principal, caller.user);
    auto generated = hierarchy_->generate_application_data_key(
        binding,
        descriptor.value().purpose);
    if (!generated) return generated.error();

    return persistent_->rotate_adopt_generated(caller, id, generated.value());
}

} // namespace os::keys
