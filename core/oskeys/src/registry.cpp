#include <os/keys/registry.hpp>

#include <os/core/identity.hpp>
#include <os/keys/error.hpp>

namespace os::keys {

KeyRecord* KeyRegistry::find(KeyId id) noexcept {
    for (auto& record : records_) {
        if (record.occupied && record.descriptor.id == id) return &record;
    }
    return nullptr;
}

const KeyRecord* KeyRegistry::find(KeyId id) const noexcept {
    for (const auto& record : records_) {
        if (record.occupied && record.descriptor.id == id) return &record;
    }
    return nullptr;
}

os::core::Result<KeyDescriptor>
KeyRegistry::create(
    KeyOwner owner,
    KeyId id,
    KeyPurpose purpose,
    RightsMask key_rights) noexcept {
    if (!os::core::valid_principal(owner.principal) || !id.valid()) {
        return key_error(errors::invalid_key);
    }
    if (!valid_purpose(purpose)) {
        return key_error(errors::unsupported_purpose);
    }
    if (!valid_rights(key_rights)) {
        return key_error(errors::invalid_rights);
    }
    if (find(id) != nullptr) {
        return key_error(errors::duplicate_key_id);
    }

    KeyRecord* free_record = nullptr;
    for (auto& record : records_) {
        if (!record.occupied) {
            free_record = &record;
            break;
        }
    }
    if (free_record == nullptr) {
        return key_error(errors::registry_full);
    }

    auto provider_key = provider_->generate(purpose);
    if (!provider_key) return provider_key.error();
    if (!provider_key.value().valid()) {
        return key_error(errors::provider_failure);
    }

    const KeyDescriptor descriptor{
        .id = id,
        .version = 1U,
        .purpose = purpose,
        .rights = key_rights,
    };
    *free_record = KeyRecord{
        .occupied = true,
        .destroyed = false,
        .owner = owner,
        .descriptor = descriptor,
        .provider_key = provider_key.value(),
    };
    return descriptor;
}

os::core::Result<KeyDescriptor>
KeyRegistry::describe(KeyOwner caller, KeyId id) const noexcept {
    const auto* record = find(id);
    if (record == nullptr) return key_error(errors::not_found);
    if (record->owner != caller) return key_error(errors::access_denied);
    if (record->destroyed) return key_error(errors::destroyed);
    return record->descriptor;
}

os::core::Result<ProviderKeyReference>
KeyRegistry::provider_reference(
    KeyOwner caller,
    KeyId id,
    RightsMask required_right) const noexcept {
    if (!valid_rights(required_right)) return key_error(errors::invalid_rights);
    const auto* record = find(id);
    if (record == nullptr) return key_error(errors::not_found);
    if (record->owner != caller) return key_error(errors::access_denied);
    if (record->destroyed) return key_error(errors::destroyed);
    if ((record->descriptor.rights & required_right) != required_right) {
        return key_error(errors::access_denied);
    }
    if (!record->provider_key.valid()) return key_error(errors::provider_failure);
    return record->provider_key;
}

os::core::Result<void>
KeyRegistry::destroy(KeyOwner caller, KeyId id) noexcept {
    auto* record = find(id);
    if (record == nullptr) return key_error(errors::not_found);
    if (record->owner != caller) return key_error(errors::access_denied);
    if (record->destroyed) return key_error(errors::destroyed);
    if ((record->descriptor.rights & rights::destroy) == 0U) {
        return key_error(errors::access_denied);
    }

    auto destroyed = provider_->destroy(record->provider_key);
    if (!destroyed) return destroyed.error();
    record->destroyed = true;
    record->provider_key = {};
    return {};
}

std::size_t KeyRegistry::record_count() const noexcept {
    std::size_t count = 0U;
    for (const auto& record : records_) {
        if (record.occupied) ++count;
    }
    return count;
}

std::size_t KeyRegistry::active_count() const noexcept {
    std::size_t count = 0U;
    for (const auto& record : records_) {
        if (record.occupied && !record.destroyed) ++count;
    }
    return count;
}

} // namespace os::keys
