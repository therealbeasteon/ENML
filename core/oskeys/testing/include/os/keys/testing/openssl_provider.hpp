#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/keys/provider.hpp>
#include <os/keys/registry.hpp>

namespace os::keys::testing {

// Host/CI-only software provider. This exists to exercise Key Service AEAD and
// lifecycle contracts on x86-64/AArch64. It is not a production hardware root
// and must never be treated as equivalent to TPM/TEE/HSM-backed protection.
class OpenSslTestKeyProvider final : public KeyProvider {
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

    std::array<Slot, max_key_records> slots_ {};
};

} // namespace os::keys::testing
