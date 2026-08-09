#include <os/keys/registry.hpp>

#include <limits>

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

KeyVersionRecord*
KeyRegistry::find_version(KeyRecord& record, std::uint32_t version) noexcept {
    for (auto& key_version : record.versions) {
        if (key_version.occupied && key_version.version == version) return &key_version;
    }
    return nullptr;
}

const KeyVersionRecord*
KeyRegistry::find_version(const KeyRecord& record, std::uint32_t version) const noexcept {
    for (const auto& key_version : record.versions) {
        if (key_version.occupied && key_version.version == version) return &key_version;
    }
    return nullptr;
}

os::core::Result<const KeyRecord*>
KeyRegistry::authorize_record(
    KeyOwner caller,
    KeyId id,
    RightsMask required_right) const noexcept {
    if (!id.valid()) return key_error(errors::invalid_key);
    if (!valid_rights(required_right)) return key_error(errors::invalid_rights);

    const auto* record = find(id);
    if (record == nullptr) return key_error(errors::not_found);
    if (record->owner != caller) return key_error(errors::access_denied);
    if (record->destroyed) return key_error(errors::destroyed);
    if ((record->descriptor.rights & required_right) != required_right) {
        return key_error(errors::access_denied);
    }
    return record;
}

os::core::Result<const KeyVersionRecord*>
KeyRegistry::authorize_version(
    KeyOwner caller,
    KeyId id,
    std::uint32_t key_version,
    RightsMask required_right,
    bool require_current) const noexcept {
    if (key_version == 0U) return key_error(errors::invalid_key);
    auto record = authorize_record(caller, id, required_right);
    if (!record) return record.error();
    if (require_current && record.value()->descriptor.version != key_version) {
        return key_error(errors::key_version_mismatch);
    }

    const auto* version = find_version(*record.value(), key_version);
    if (version == nullptr) return key_error(errors::key_version_mismatch);
    if (version->destroyed) return key_error(errors::destroyed);
    if (!version->provider_key.valid()) return key_error(errors::provider_failure);
    return version;
}

os::core::Result<KeyDescriptor>
KeyRegistry::create(
    KeyOwner owner,
    KeyId id,
    KeyPurpose purpose,
    RightsMask granted_rights) noexcept {
    if (!os::core::valid_principal(owner.principal) || !id.valid()) {
        return key_error(errors::invalid_key);
    }
    if (!valid_purpose(purpose)) {
        return key_error(errors::unsupported_purpose);
    }
    if (!valid_rights(granted_rights)) {
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
        .rights = granted_rights,
    };
    *free_record = KeyRecord{
        .occupied = true,
        .destroyed = false,
        .owner = owner,
        .descriptor = descriptor,
        .versions = {},
    };
    free_record->versions[0] = KeyVersionRecord{
        .occupied = true,
        .destroyed = false,
        .version = 1U,
        .provider_key = provider_key.value(),
    };
    return descriptor;
}

os::core::Result<KeyDescriptor>
KeyRegistry::describe(KeyOwner caller, KeyId id) const noexcept {
    auto record = authorize_record(caller, id, key_rights::metadata);
    if (!record) return record.error();
    return record.value()->descriptor;
}

os::core::Result<KeyDescriptor>
KeyRegistry::rotate(KeyOwner caller, KeyId id) noexcept {
    auto authorized = authorize_record(caller, id, key_rights::rotate);
    if (!authorized) return authorized.error();

    auto* record = find(id);
    if (record == nullptr) return key_error(errors::not_found);
    if (record->descriptor.version == std::numeric_limits<std::uint32_t>::max()) {
        return key_error(errors::version_limit);
    }

    KeyVersionRecord* free_version = nullptr;
    for (auto& version : record->versions) {
        if (!version.occupied) {
            free_version = &version;
            break;
        }
    }
    if (free_version == nullptr) return key_error(errors::version_limit);

    auto provider_key = provider_->generate(record->descriptor.purpose);
    if (!provider_key) return provider_key.error();
    if (!provider_key.value().valid()) return key_error(errors::provider_failure);

    const std::uint32_t new_version = record->descriptor.version + 1U;
    *free_version = KeyVersionRecord{
        .occupied = true,
        .destroyed = false,
        .version = new_version,
        .provider_key = provider_key.value(),
    };
    record->descriptor.version = new_version;
    return record->descriptor;
}

os::core::Result<ProviderKeyReference>
KeyRegistry::provider_reference(
    KeyOwner caller,
    KeyId id,
    RightsMask required_right) const noexcept {
    const auto* record = find(id);
    if (record == nullptr) return key_error(errors::not_found);
    auto version = authorize_version(
        caller,
        id,
        record->descriptor.version,
        required_right,
        true);
    if (!version) return version.error();
    return version.value()->provider_key;
}

os::core::Result<std::size_t>
KeyRegistry::seal(
    KeyOwner caller,
    KeyId id,
    std::uint32_t key_version,
    CryptoProfileId profile,
    os::core::ByteSpan envelope_aad,
    os::core::ByteSpan caller_aad,
    os::core::ByteSpan plaintext,
    os::core::MutableByteSpan ciphertext,
    AeadNonce& nonce,
    AeadTag& tag) noexcept {
    auto version = authorize_version(
        caller,
        id,
        key_version,
        key_rights::encrypt,
        true);
    if (!version) return version.error();
    return provider_->seal(
        version.value()->provider_key,
        profile,
        envelope_aad,
        caller_aad,
        plaintext,
        ciphertext,
        nonce,
        tag);
}

os::core::Result<std::size_t>
KeyRegistry::open(
    KeyOwner caller,
    KeyId id,
    std::uint32_t key_version,
    CryptoProfileId profile,
    os::core::ByteSpan envelope_aad,
    os::core::ByteSpan caller_aad,
    const AeadNonce& nonce,
    const AeadTag& tag,
    os::core::ByteSpan ciphertext,
    os::core::MutableByteSpan plaintext) noexcept {
    auto version = authorize_version(
        caller,
        id,
        key_version,
        key_rights::decrypt,
        false);
    if (!version) return version.error();
    return provider_->open(
        version.value()->provider_key,
        profile,
        envelope_aad,
        caller_aad,
        nonce,
        tag,
        ciphertext,
        plaintext);
}

os::core::Result<void>
KeyRegistry::destroy(KeyOwner caller, KeyId id) noexcept {
    auto authorized = authorize_record(caller, id, key_rights::destroy);
    if (!authorized) return authorized.error();

    auto* record = find(id);
    if (record == nullptr) return key_error(errors::not_found);

    os::core::Error first_failure{};
    bool failed = false;
    for (auto& version : record->versions) {
        if (!version.occupied || version.destroyed) continue;
        auto destroyed = provider_->destroy(version.provider_key);
        if (!destroyed) {
            if (!failed) first_failure = destroyed.error();
            failed = true;
            continue;
        }
        version.destroyed = true;
        version.provider_key = {};
    }
    if (failed) return first_failure;

    record->destroyed = true;
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

std::size_t KeyRegistry::version_count(KeyId id) const noexcept {
    const auto* record = find(id);
    if (record == nullptr) return 0U;
    std::size_t count = 0U;
    for (const auto& version : record->versions) {
        if (version.occupied) ++count;
    }
    return count;
}

} // namespace os::keys
