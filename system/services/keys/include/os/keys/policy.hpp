#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/keys/key.hpp>
#include <os/keys/registry.hpp>

namespace os::keys {

inline constexpr std::size_t max_application_key_policies = 64U;

// Trusted service-local admission policy. KeyOwner values are published only by
// system control code; public application requests never provide this identity.
// A policy entry permits that durable PrincipalId+UserId to acquire/use Key
// Service capabilities. Removing it is an authority revocation, not key/data
// destruction.
class ApplicationKeyPolicy final {
public:
    [[nodiscard]] os::core::Result<void>
    enable(KeyOwner owner) noexcept;

    [[nodiscard]] os::core::Result<void>
    disable(KeyOwner owner) noexcept;

    [[nodiscard]] bool enabled(KeyOwner owner) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct Entry final {
        bool occupied {false};
        KeyOwner owner {};
    };

    std::array<Entry, max_application_key_policies> entries_ {};
};

// Trusted lifecycle admission in front of any KeyStore backend, including
// PersistentKeyRegistry. KeyOwner reaches this wrapper only after Key Service
// resolved RequestContext.peer; the wrapper never accepts identity from app
// payloads and does not change the public M2.4-M2.6 wire protocol.
class PolicyKeyStore final : public KeyStore {
public:
    PolicyKeyStore(KeyStore& backend, const ApplicationKeyPolicy& policy) noexcept
        : backend_(&backend), policy_(&policy) {}

    [[nodiscard]] os::core::Result<KeyDescriptor>
    create(
        KeyOwner owner,
        KeyId id,
        KeyPurpose purpose,
        RightsMask rights) noexcept override;

    [[nodiscard]] os::core::Result<KeyDescriptor>
    describe(KeyOwner caller, KeyId id) const noexcept override;

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
        AeadTag& tag) noexcept override;

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
        os::core::MutableByteSpan plaintext) noexcept override;

    [[nodiscard]] os::core::Result<void>
    destroy(KeyOwner caller, KeyId id) noexcept override;

private:
    [[nodiscard]] os::core::Result<void>
    authorize(KeyOwner owner) const noexcept;

    KeyStore* backend_ {nullptr};
    const ApplicationKeyPolicy* policy_ {nullptr};
};

} // namespace os::keys
