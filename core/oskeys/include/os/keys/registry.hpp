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

// Fixed-capacity metadata registry. Public KeyId values are locators, not
// authority: every lookup is checked against the trusted caller owner.
// A logical KeyId may retain several provider-owned key versions so rotation
// can move new encryption forward without making existing ciphertext
// undecryptable. Destroyed records remain tombstones and are never silently
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

    [[nodiscard]] os::core::Result<KeyDescriptor>
    rotate(KeyOwner caller, KeyId id) noexcept;

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
    [[nodiscard]] std::size_t version_count(KeyId id) const noexcept;

private:
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
