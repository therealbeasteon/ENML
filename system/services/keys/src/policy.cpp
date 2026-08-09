#include <os/keys/policy.hpp>

#include <os/core/identity.hpp>
#include <os/keys/error.hpp>

namespace os::keys {

os::core::Result<void>
ApplicationKeyPolicy::enable(KeyOwner owner) noexcept {
    if (!os::core::valid_principal(owner.principal)) {
        return key_error(errors::invalid_key);
    }

    for (const auto& entry : entries_) {
        if (entry.occupied && entry.owner == owner) return {};
        if (entry.occupied && entry.owner.principal == owner.principal &&
            entry.owner.user != owner.user) {
            return key_error(errors::hierarchy_conflict);
        }
    }

    for (auto& entry : entries_) {
        if (!entry.occupied) {
            entry = Entry{.occupied = true, .owner = owner};
            return {};
        }
    }
    return key_error(errors::policy_capacity);
}

os::core::Result<void>
ApplicationKeyPolicy::disable(KeyOwner owner) noexcept {
    if (!os::core::valid_principal(owner.principal)) {
        return key_error(errors::invalid_key);
    }
    for (auto& entry : entries_) {
        if (entry.occupied && entry.owner == owner) {
            entry = Entry{};
            return {};
        }
    }
    return key_error(errors::policy_not_registered);
}

bool ApplicationKeyPolicy::enabled(KeyOwner owner) const noexcept {
    for (const auto& entry : entries_) {
        if (entry.occupied && entry.owner == owner) return true;
    }
    return false;
}

std::size_t ApplicationKeyPolicy::size() const noexcept {
    std::size_t count = 0U;
    for (const auto& entry : entries_) {
        if (entry.occupied) ++count;
    }
    return count;
}

os::core::Result<void>
PolicyKeyStore::authorize(KeyOwner owner) const noexcept {
    if (backend_ == nullptr || policy_ == nullptr) {
        return key_error(errors::provider_failure);
    }
    if (!policy_->enabled(owner)) {
        return key_error(errors::policy_not_registered);
    }
    return {};
}

os::core::Result<KeyDescriptor>
PolicyKeyStore::create(
    KeyOwner owner,
    KeyId id,
    KeyPurpose purpose,
    RightsMask rights) noexcept {
    auto allowed = authorize(owner);
    if (!allowed) return allowed.error();
    return backend_->create(owner, id, purpose, rights);
}

os::core::Result<KeyDescriptor>
PolicyKeyStore::describe(KeyOwner caller, KeyId id) const noexcept {
    auto allowed = authorize(caller);
    if (!allowed) return allowed.error();
    return backend_->describe(caller, id);
}

os::core::Result<KeyDescriptor>
PolicyKeyStore::rotate(KeyOwner caller, KeyId id) noexcept {
    auto allowed = authorize(caller);
    if (!allowed) return allowed.error();
    return backend_->rotate(caller, id);
}

os::core::Result<std::size_t>
PolicyKeyStore::seal(
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
    auto allowed = authorize(caller);
    if (!allowed) return allowed.error();
    return backend_->seal(
        caller,
        id,
        key_version,
        profile,
        envelope_aad,
        caller_aad,
        plaintext,
        ciphertext,
        nonce,
        tag);
}

os::core::Result<std::size_t>
PolicyKeyStore::open(
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
    auto allowed = authorize(caller);
    if (!allowed) return allowed.error();
    return backend_->open(
        caller,
        id,
        key_version,
        profile,
        envelope_aad,
        caller_aad,
        nonce,
        tag,
        ciphertext,
        plaintext);
}

os::core::Result<void>
PolicyKeyStore::destroy(KeyOwner caller, KeyId id) noexcept {
    auto allowed = authorize(caller);
    if (!allowed) return allowed.error();
    return backend_->destroy(caller, id);
}

} // namespace os::keys
