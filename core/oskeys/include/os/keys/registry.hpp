#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/keys/crypto.hpp>
#include <os/keys/key.hpp>
#include <os/keys/provider.hpp>

namespace os::keys {

inline constexpr std::size_t max_key_records = 128U;

struct KeyRecord final {
    bool occupied {false};
    bool destroyed {false};
    KeyOwner owner {};
    KeyDescriptor descriptor {};
    ProviderKeyReference provider_key {};
};

// Fixed-capacity metadata registry. Public KeyId values are locators, not
// authority: every lookup is still checked against the trusted caller owner.
// Destroyed records remain tombstones so a logical KeyId cannot be silently
// reused within this registry generation.
class KeyRegistry final {
public:
    explicit KeyRegistry(KeyProvider& provider) noexcept : provider_(&provider) {}

    [[nodiscard]] os::core::Result<KeyDescriptor>
    create(
        KeyOwner owner,
        KeyId id,
        KeyPurpose purpose,
        RightsMask rights) noexcept;

    [[nodiscard]] os::core::Result<KeyDescriptor>
    describe(KeyOwner caller, KeyId id) const noexcept;

    [[nodiscard]] os::core::Result<ProviderKeyReference>
    provider_reference(KeyOwner caller, KeyId id, RightsMask required_right) const noexcept;

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
        AeadTag& tag) noexcept;

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
        os::core::MutableByteSpan plaintext) noexcept;

    [[nodiscard]] os::core::Result<void>
    destroy(KeyOwner caller, KeyId id) noexcept;

    [[nodiscard]] std::size_t record_count() const noexcept;
    [[nodiscard]] std::size_t active_count() const noexcept;

private:
    [[nodiscard]] KeyRecord* find(KeyId id) noexcept;
    [[nodiscard]] const KeyRecord* find(KeyId id) const noexcept;

    [[nodiscard]] os::core::Result<const KeyRecord*>
    authorize(
        KeyOwner caller,
        KeyId id,
        std::uint32_t key_version,
        RightsMask required_right) const noexcept;

    KeyProvider* provider_ {nullptr};
    std::array<KeyRecord, max_key_records> records_ {};
};

} // namespace os::keys
