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
inline constexpr std::size_t key_registry_binding_bytes_v1 = 56U;

// Durable logical-key registry. The caller provides an already-authorized
// state-directory handle and a provider that can persist opaque wrapped/sealed
// provider objects. Raw long-lived key bytes never enter the registry snapshot.
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

    [[nodiscard]] os::core::Result<KeyDescriptor>
    describe(KeyOwner caller, KeyId id) const noexcept override {
        return registry_.describe(caller, id);
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
    persist_candidate(const KeyRegistry& candidate) noexcept;

    static void cleanup_provider_references(
        PersistentKeyProvider& provider,
        KeyRegistry& registry) noexcept;

    os::core::NativeHandle state_directory_ {};
    PersistentKeyProvider* provider_ {nullptr};
    KeyRegistry registry_;
};

} // namespace os::keys
