#pragma once

#include <cstddef>

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/keys/crypto.hpp>
#include <os/keys/key.hpp>

namespace os::keys {

inline constexpr std::size_t max_persistent_provider_blob_bytes = 256U;

// Key providers own secret material. ENML core/service code receives only an
// opaque provider reference and never asks a provider to export a long-lived
// raw key. Hardware-backed TPM/TEE/HSM providers can implement this interface
// later without changing public application key identities.
class KeyProvider {
public:
    virtual ~KeyProvider() = default;

    [[nodiscard]] virtual os::core::Result<ProviderKeyReference>
    generate(KeyPurpose purpose) noexcept = 0;

    // The provider generates a fresh nonce for every seal operation. ENML
    // passes canonical envelope metadata separately from caller AAD so the
    // provider can authenticate both without requiring an intermediate heap
    // concatenation buffer.
    [[nodiscard]] virtual os::core::Result<std::size_t>
    seal(
        ProviderKeyReference key,
        CryptoProfileId profile,
        os::core::ByteSpan envelope_aad,
        os::core::ByteSpan caller_aad,
        os::core::ByteSpan plaintext,
        os::core::MutableByteSpan ciphertext,
        AeadNonce& nonce,
        AeadTag& tag) noexcept = 0;

    [[nodiscard]] virtual os::core::Result<std::size_t>
    open(
        ProviderKeyReference key,
        CryptoProfileId profile,
        os::core::ByteSpan envelope_aad,
        os::core::ByteSpan caller_aad,
        const AeadNonce& nonce,
        const AeadTag& tag,
        os::core::ByteSpan ciphertext,
        os::core::MutableByteSpan plaintext) noexcept = 0;

    [[nodiscard]] virtual os::core::Result<void>
    destroy(ProviderKeyReference key) noexcept = 0;
};

// Internal durability extension for providers that can make a key survive a
// Key Service restart. The returned bytes are an opaque provider representation
// and MUST NOT be an unwrapped long-lived key. A production hardware provider
// may encode a sealed/wrapped key or a durable secure-object locator. The core
// never interprets these bytes. `binding` is canonical registry metadata that
// the provider must authenticate so a persisted provider object cannot be
// transplanted to a different logical key/version/owner record.
class PersistentKeyProvider : public KeyProvider {
public:
    ~PersistentKeyProvider() override = default;

    [[nodiscard]] virtual os::core::Result<std::size_t>
    persist_reference(
        ProviderKeyReference key,
        KeyPurpose purpose,
        os::core::ByteSpan binding,
        os::core::MutableByteSpan output) noexcept = 0;

    [[nodiscard]] virtual os::core::Result<ProviderKeyReference>
    restore_reference(
        KeyPurpose purpose,
        os::core::ByteSpan binding,
        os::core::ByteSpan persistent_blob) noexcept = 0;
};

} // namespace os::keys
