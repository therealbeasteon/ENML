#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/keys/hierarchy.hpp>
#include <os/keys/persistence.hpp>
#include <os/keys/policy.hpp>

namespace os::keys {

// Product KeyStore composition for M2.8. Admission comes from trusted lifecycle
// policy, v1/new-version provider material is generated beneath the caller's
// M2.7 application root, and durable metadata/public operations stay in the
// existing PersistentKeyRegistry. This changes no public Key Service wire ABI.
class HierarchicalPolicyKeyStore final : public KeyStore {
public:
    HierarchicalPolicyKeyStore(
        PersistentKeyRegistry& persistent,
        KeyHierarchy& hierarchy,
        const ApplicationKeyPolicy& policy) noexcept
        : persistent_(&persistent),
          hierarchy_(&hierarchy),
          policy_(&policy),
          gated_(persistent, policy) {}

    [[nodiscard]] os::core::Result<KeyDescriptor>
    create(
        KeyOwner owner,
        KeyId id,
        KeyPurpose purpose,
        RightsMask rights) noexcept override;

    [[nodiscard]] os::core::Result<KeyDescriptor>
    describe(KeyOwner caller, KeyId id) const noexcept override {
        return gated_.describe(caller, id);
    }

    [[nodiscard]] os::core::Result<KeyDescriptor>
    rotate(KeyOwner caller, KeyId id) noexcept override;

    [[nodiscard]] os::core::Result<std::size_t>
    seal(
        KeyOwner caller,
        KeyId id,
        std::uint32_t key_version,
        CryptoProfileId profile,
        os::core::ByteSpan envelope_aad,
        os::core::ByteSpan caller_aad,
        os::core::ByteSpan plaintext,
        os::core::MutableByteSpan ciphertext,
        AeadNonce& nonce,
        AeadTag& tag) noexcept override {
        return gated_.seal(
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

    [[nodiscard]] os::core::Result<std::size_t>
    open(
        KeyOwner caller,
        KeyId id,
        std::uint32_t key_version,
        CryptoProfileId profile,
        os::core::ByteSpan envelope_aad,
        os::core::ByteSpan caller_aad,
        const AeadNonce& nonce,
        const AeadTag& tag,
        os::core::ByteSpan ciphertext,
        os::core::MutableByteSpan plaintext) noexcept override {
        return gated_.open(
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

    [[nodiscard]] os::core::Result<void>
    destroy(KeyOwner caller, KeyId id) noexcept override {
        return gated_.destroy(caller, id);
    }

private:
    [[nodiscard]] os::core::Result<void>
    authorize(KeyOwner owner) const noexcept;

    PersistentKeyRegistry* persistent_ {nullptr};
    KeyHierarchy* hierarchy_ {nullptr};
    const ApplicationKeyPolicy* policy_ {nullptr};
    PolicyKeyStore gated_;
};

} // namespace os::keys
