#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/keys/provider.hpp>
#include <os/keys/registry.hpp>

namespace os::keys {

inline constexpr std::uint32_t key_registry_snapshot_magic_v1 = 0x3147524BU; // "KRG1" LE
inline constexpr std::uint16_t key_registry_snapshot_version_v1 = 1U;
inline constexpr std::uint16_t key_registry_snapshot_header_size_v1 = 32U;
inline constexpr std::size_t max_key_registry_snapshot_bytes = 288U * 1024U;
inline constexpr std::size_t key_registry_binding_bytes_v1 = 60U;

class PersistentKeyRegistry final : public KeyStore {
public:
    PersistentKeyRegistry(const PersistentKeyRegistry&) = delete;
    PersistentKeyRegistry& operator=(const PersistentKeyRegistry&) = delete;
    PersistentKeyRegistry(PersistentKeyRegistry&&) noexcept = default;
    PersistentKeyRegistry& operator=(PersistentKeyRegistry&&) noexcept = default;
    ~PersistentKeyRegistry() override = default;

    [[nodiscard]] static os::core::Result<PersistentKeyRegistry>
    open(os::core::NativeHandle state_directory, PersistentKeyProvider& provider) noexcept;

    [[nodiscard]] os::core::Result<KeyDescriptor>
    create(
        KeyOwner owner,
        KeyId id,
        KeyPurpose purpose,
        RightsMask rights) noexcept override;

    // Trusted internal variant for provider material already generated beneath
    // the correct M2.7 application root. The persistent registry takes
    // ownership of `provider_key` on entry: if publication fails before rename
    // it destroys the generated provider object; if replacement occurred it
    // preserves the provider object referenced by the visible snapshot.
    [[nodiscard]] os::core::Result<KeyDescriptor>
    adopt_generated(
        KeyOwner owner,
        KeyId id,
        KeyPurpose purpose,
        RightsMask rights,
        ProviderKeyReference provider_key) noexcept;

    [[nodiscard]] os::core::Result<KeyDescriptor>
    describe(KeyOwner caller, KeyId id) const noexcept override {
        return registry_.describe(caller, id);
    }

    [[nodiscard]] os::core::Result<KeyDescriptor>
    rotate(KeyOwner caller, KeyId id) noexcept override;

    // Trusted rotation variant for provider material generated beneath the
    // existing application root. Ownership/replacement semantics mirror
    // adopt_generated().
    [[nodiscard]] os::core::Result<KeyDescriptor>
    rotate_adopt_generated(
        KeyOwner caller,
        KeyId id,
        ProviderKeyReference provider_key) noexcept;

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
        return registry_.seal(
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
        return registry_.open(
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
    destroy(KeyOwner caller, KeyId id) noexcept override;

    [[nodiscard]] std::size_t record_count() const noexcept { return registry_.record_count(); }
    [[nodiscard]] std::size_t active_count() const noexcept { return registry_.active_count(); }
    [[nodiscard]] std::size_t version_count(KeyId id) const noexcept {
        return registry_.version_count(id);
    }

    [[nodiscard]] const KeyRegistry& registry() const noexcept { return registry_; }

private:
    PersistentKeyRegistry(
        os::core::NativeHandle state_directory,
        PersistentKeyProvider& provider,
        KeyRegistry registry) noexcept
        : state_directory_(static_cast<os::core::NativeHandle&&>(state_directory)),
          provider_(&provider),
          registry_(registry) {}

    [[nodiscard]] static os::core::Result<KeyRegistry>
    load_snapshot(int directory_fd, PersistentKeyProvider& provider) noexcept;

    [[nodiscard]] os::core::Result<void>
    persist_candidate(const KeyRegistry& candidate, bool& replaced) noexcept;

    static void cleanup_provider_references(
        PersistentKeyProvider& provider,
        KeyRegistry& registry) noexcept;

    os::core::NativeHandle state_directory_ {};
    PersistentKeyProvider* provider_ {nullptr};
    KeyRegistry registry_;
};

} // namespace os::keys
