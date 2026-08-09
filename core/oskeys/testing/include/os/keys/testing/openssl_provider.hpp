#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/keys/provider.hpp>
#include <os/keys/registry.hpp>

namespace os::keys::testing {

// Host/CI-only software provider. This exists to exercise Key Service AEAD,
// lifecycle, rotation and persistence contracts on x86-64/AArch64. Its fixed
// wrapping key is deliberately test-only and is NOT a production hardware root.
class OpenSslTestKeyProvider final : public PersistentKeyProvider {
public:
    OpenSslTestKeyProvider() noexcept = default;
    ~OpenSslTestKeyProvider() override;

    OpenSslTestKeyProvider(const OpenSslTestKeyProvider&) = delete;
    OpenSslTestKeyProvider& operator=(const OpenSslTestKeyProvider&) = delete;

    [[nodiscard]] os::core::Result<ProviderKeyReference>
    generate(KeyPurpose purpose) noexcept override;

    [[nodiscard]] os::core::Result<std::size_t>
    seal(
        ProviderKeyReference key,
        CryptoProfileId profile,
        os::core::ByteSpan envelope_aad,
        os::core::ByteSpan caller_aad,
        os::core::ByteSpan plaintext,
        os::core::MutableByteSpan ciphertext,
        AeadNonce& nonce,
        AeadTag& tag) noexcept override;

    [[nodiscard]] os::core::Result<std::size_t>
    open(
        ProviderKeyReference key,
        CryptoProfileId profile,
        os::core::ByteSpan envelope_aad,
        os::core::ByteSpan caller_aad,
        const AeadNonce& nonce,
        const AeadTag& tag,
        os::core::ByteSpan ciphertext,
        os::core::MutableByteSpan plaintext) noexcept override;

    [[nodiscard]] os::core::Result<void>
    destroy(ProviderKeyReference key) noexcept override;

    [[nodiscard]] os::core::Result<std::size_t>
    persist_reference(
        ProviderKeyReference key,
        KeyPurpose purpose,
        os::core::MutableByteSpan output) noexcept override;

    [[nodiscard]] os::core::Result<ProviderKeyReference>
    restore_reference(
        KeyPurpose purpose,
        os::core::ByteSpan persistent_blob) noexcept override;

private:
    static constexpr std::size_t key_bytes = 32U;

    struct Slot final {
        bool occupied {false};
        std::uint32_t generation {0U};
        std::array<std::byte, key_bytes> key {};
    };

    [[nodiscard]] Slot* resolve(ProviderKeyReference reference) noexcept;
    [[nodiscard]] const Slot* resolve(ProviderKeyReference reference) const noexcept;
    [[nodiscard]] static ProviderKeyReference make_reference(
        std::size_t index,
        std::uint32_t generation) noexcept;
    [[nodiscard]] os::core::Result<ProviderKeyReference>
    install_key(os::core::ByteSpan key_material) noexcept;

    std::array<Slot, max_key_records * max_key_versions> slots_ {};
};

} // namespace os::keys::testing
