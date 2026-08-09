#pragma once

#include <cstddef>

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/keys/crypto.hpp>
#include <os/keys/key.hpp>

namespace os::keys {

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

} // namespace os::keys
