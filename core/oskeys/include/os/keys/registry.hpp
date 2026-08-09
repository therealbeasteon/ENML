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
inline constexpr std::size_t max_key_versions = 8U;

struct KeyVersionRecord final {
    bool occupied {false};
    bool destroyed {false};
    std::uint32_t version {0U};
    ProviderKeyReference provider_key {};
};

struct KeyRecord final {
    bool occupied {false};
    bool destroyed {false};
    KeyOwner owner {};
    KeyDescriptor descriptor {};
    std::array<KeyVersionRecord, max_key_versions> versions {};
};

class KeyStore {
public:
    virtual ~KeyStore() = default;

    [[nodiscard]] virtual os::core::Result<KeyDescriptor>
    create(
        KeyOwner owner,
        KeyId id,
        KeyPurpose purpose,
        RightsMask rights) noexcept = 0;

    [[nodiscard]] virtual os::core::Result<KeyDescriptor>
    describe(KeyOwner caller, KeyId id) const noexcept = 0;

    [[nodiscard]] virtual os::core::Result<KeyDescriptor>
    rotate(KeyOwner caller, KeyId id) noexcept = 0;

    [[nodiscard]] virtual os::core::Result<std::size_t>
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
        AeadTag& tag) noexcept = 0;

    [[nodiscard]] virtual os::core::Result<std::size_t>
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
        os::core::MutableByteSpan plaintext) noexcept = 0;

    [[nodiscard]] virtual os::core::Result<void>
    destroy(KeyOwner caller, KeyId id) noexcept = 0;
};

class PersistentKeyRegistry;

class KeyRegistry final : public KeyStore {
public:
    explicit KeyRegistry(KeyProvider& provider) noexcept : provider_(&provider) {}

    [[nodiscard]] os::core::Result<KeyDescriptor>
    create(
        KeyOwner owner,
        KeyId id,
        KeyPurpose purpose,
        RightsMask rights) noexcept override;

    // Trusted internal construction hook used when a reviewed hierarchy/root
    // provider has already generated the provider-owned secret under the
    // correct application root. This never crosses public IPC. On failure the
    // caller still owns `provider_key` and must destroy it.
    [[nodiscard]] os::core::Result<KeyDescriptor>
    adopt_generated(
        KeyOwner owner,
        KeyId id,
        KeyPurpose purpose,
        RightsMask rights,
        ProviderKeyReference provider_key) noexcept;

    [[nodiscard]] os::core::Result<KeyDescriptor>
    describe(KeyOwner caller, KeyId id) const noexcept override;

    [[nodiscard]] os::core::Result<KeyDescriptor>
    rotate(KeyOwner caller, KeyId id) noexcept override;

    // Rotation counterpart to adopt_generated(). Authorization/version bounds
    // are identical to rotate(), but provider material was generated beneath a
    // trusted M2.7 application root. Caller owns provider_key on failure.
    [[nodiscard]] os::core::Result<KeyDescriptor>
    rotate_adopt_generated(
        KeyOwner caller,
        KeyId id,
        ProviderKeyReference provider_key) noexcept;

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

    [[nodiscard]] std::size_t record_count() const noexcept;
    [[nodiscard]] std::size_t active_count() const noexcept;
    [[nodiscard]] std::size_t version_count(KeyId id) const noexcept;

private:
    friend class PersistentKeyRegistry;

    [[nodiscard]] KeyRecord* find(KeyId id) noexcept;
    [[nodiscard]] const KeyRecord* find(KeyId id) const noexcept;
    [[nodiscard]] KeyVersionRecord* find_version(KeyRecord& record, std::uint32_t version) noexcept;
    [[nodiscard]] const KeyVersionRecord*
    find_version(const KeyRecord& record, std::uint32_t version) const noexcept;

    [[nodiscard]] os::core::Result<const KeyRecord*>
    authorize_record(KeyOwner caller, KeyId id, RightsMask required_right) const noexcept;

    [[nodiscard]] os::core::Result<const KeyVersionRecord*>
    authorize_version(
        KeyOwner caller,
        KeyId id,
        std::uint32_t key_version,
        RightsMask required_right,
        bool require_current) const noexcept;

    KeyProvider* provider_ {nullptr};
    std::array<KeyRecord, max_key_records> records_ {};
};

} // namespace os::keys
