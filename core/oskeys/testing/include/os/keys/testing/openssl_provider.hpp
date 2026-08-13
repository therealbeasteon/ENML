#pragma once

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>

#include <os/keys/hierarchy.hpp>
#include <os/keys/provider.hpp>
#include <os/keys/registry.hpp>

namespace os::keys::testing {

// Host/CI-only software provider. This exists to exercise Key Service AEAD,
// lifecycle, rotation, persistence and hierarchy contracts on x86-64/AArch64.
// Its fixed wrapping key and software root table are deliberately test-only and
// are NOT a production TPM/TEE/HSM root.
class OpenSslTestKeyProvider final : public HierarchicalKeyProvider {
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
        os::core::ByteSpan binding,
        os::core::MutableByteSpan output) noexcept override;

    [[nodiscard]] os::core::Result<ProviderKeyReference>
    restore_reference(
        KeyPurpose purpose,
        os::core::ByteSpan binding,
        os::core::ByteSpan persistent_blob) noexcept override;

    [[nodiscard]] os::core::Result<RootKeyReference>
    acquire_system_root(KeyProtectionBinding system_binding) noexcept override;

    [[nodiscard]] os::core::Result<RootKeyReference>
    acquire_child_root(
        RootKeyReference parent,
        KeyProtectionBinding parent_binding,
        KeyProtectionBinding child_binding) noexcept override;

    [[nodiscard]] os::core::Result<ProviderKeyReference>
    generate_under_root(
        RootKeyReference root,
        KeyProtectionBinding binding,
        KeyPurpose purpose) noexcept override;

    [[nodiscard]] os::core::Result<void>
    destroy_root(RootKeyReference root, KeyProtectionBinding binding) noexcept override;

private:
    static constexpr std::size_t key_bytes = 32U;
    static constexpr std::size_t max_root_slots =
        1U + max_profile_roots + max_application_roots;

    struct Slot final {
        bool occupied {false};
        std::uint32_t generation {0U};
        std::array<std::byte, key_bytes> key {};
    };

    struct RootSlot final {
        bool occupied {false};
        std::uint32_t generation {0U};
        KeyProtectionBinding binding {};
        RootKeyReference parent {};
    };

    [[nodiscard]] Slot* resolve(ProviderKeyReference reference) noexcept;
    [[nodiscard]] const Slot* resolve(ProviderKeyReference reference) const noexcept;
    [[nodiscard]] static ProviderKeyReference make_reference(
        std::size_t index,
        std::uint32_t generation) noexcept;
    [[nodiscard]] os::core::Result<ProviderKeyReference>
    install_key(os::core::ByteSpan key_material) noexcept;

    // Key material creation with no purpose policy of its own. Both entry
    // points check first and then call this; neither the flat nor the
    // root-scoped admission rule lives here, so widening one cannot silently
    // widen the other.
    [[nodiscard]] os::core::Result<ProviderKeyReference> generate_material() noexcept;

    [[nodiscard]] RootSlot* resolve_root(RootKeyReference reference) noexcept;
    [[nodiscard]] const RootSlot* resolve_root(RootKeyReference reference) const noexcept;
    [[nodiscard]] static RootKeyReference make_root_reference(
        std::size_t index,
        std::uint32_t generation) noexcept;
    [[nodiscard]] os::core::Result<RootKeyReference>
    install_root(KeyProtectionBinding binding, RootKeyReference parent) noexcept;

    // Legacy unbound helper bodies kept in the original test-provider source
    // while M2.6 introduces binding-aware persistence in a separate translation
    // unit. They are private and never used by the persistence boundary.
    [[nodiscard]] os::core::Result<std::size_t>
    persist_reference(
        ProviderKeyReference key,
        KeyPurpose purpose,
        os::core::MutableByteSpan output) noexcept;

    [[nodiscard]] os::core::Result<ProviderKeyReference>
    restore_reference(
        KeyPurpose purpose,
        os::core::ByteSpan persistent_blob) noexcept;

    std::array<Slot, max_key_records * max_key_versions> slots_ {};
    std::array<RootSlot, max_root_slots> root_slots_ {};
};

} // namespace os::keys::testing
