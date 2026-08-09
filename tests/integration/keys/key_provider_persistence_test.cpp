#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <string_view>

#include <os/core/span.hpp>
#include <os/keys/crypto.hpp>
#include <os/keys/error.hpp>
#include <os/keys/provider.hpp>
#include <os/keys/testing/openssl_provider.hpp>

namespace {

[[nodiscard]] os::core::ByteSpan as_bytes(std::string_view text) noexcept {
    return {
        reinterpret_cast<const std::byte*>(text.data()),
        text.size(),
    };
}

} // namespace

int main() {
    constexpr auto purpose = os::keys::KeyPurpose::application_data_aead;
    constexpr auto profile = os::keys::CryptoProfileId::aes_256_gcm_v1;
    constexpr std::string_view plaintext_text = "provider restart persistence proof";
    constexpr std::string_view header_text = "EKEY-header-fixture";
    constexpr std::string_view caller_aad_text = "principal:70;object:persistent";

    os::keys::testing::OpenSslTestKeyProvider first_provider;
    auto generated = first_provider.generate(purpose);
    assert(generated);

    std::array<std::byte, os::keys::max_key_plaintext_bytes> ciphertext{};
    os::keys::AeadNonce nonce{};
    os::keys::AeadTag tag{};
    auto sealed = first_provider.seal(
        generated.value(),
        profile,
        as_bytes(header_text),
        as_bytes(caller_aad_text),
        as_bytes(plaintext_text),
        ciphertext,
        nonce,
        tag);
    assert(sealed);
    assert(sealed.value() == plaintext_text.size());

    std::array<std::byte, os::keys::max_persistent_provider_blob_bytes> persistent_blob{};
    auto persisted = first_provider.persist_reference(
        generated.value(), purpose, persistent_blob);
    assert(persisted);
    assert(persisted.value() > 32U);
    assert(persisted.value() <= persistent_blob.size());

    // A fresh provider instance represents a Key Service restart. The opaque
    // provider blob must restore usable key authority without exporting raw key
    // bytes through the core API.
    os::keys::testing::OpenSslTestKeyProvider restarted_provider;
    auto restored = restarted_provider.restore_reference(
        purpose,
        {persistent_blob.data(), persisted.value()});
    assert(restored);
    assert(restored.value().valid());

    std::array<std::byte, os::keys::max_key_plaintext_bytes> plaintext_output{};
    auto opened = restarted_provider.open(
        restored.value(),
        profile,
        as_bytes(header_text),
        as_bytes(caller_aad_text),
        nonce,
        tag,
        {ciphertext.data(), sealed.value()},
        plaintext_output);
    assert(opened);
    assert(opened.value() == plaintext_text.size());
    assert(std::equal(
        plaintext_output.begin(),
        plaintext_output.begin() + static_cast<std::ptrdiff_t>(opened.value()),
        as_bytes(plaintext_text).begin()));

    auto tampered_blob = persistent_blob;
    tampered_blob[persisted.value() - 1U] ^= std::byte{0x01};
    auto tampered_restore = restarted_provider.restore_reference(
        purpose,
        {tampered_blob.data(), persisted.value()});
    assert(!tampered_restore);
    assert(tampered_restore.error() ==
        os::keys::key_error(os::keys::errors::authentication_failed));

    auto truncated_restore = restarted_provider.restore_reference(
        purpose,
        {persistent_blob.data(), persisted.value() - 1U});
    assert(!truncated_restore);
    assert(truncated_restore.error() ==
        os::keys::key_error(os::keys::errors::provider_failure));

    assert(first_provider.destroy(generated.value()));
    auto persist_destroyed = first_provider.persist_reference(
        generated.value(), purpose, persistent_blob);
    assert(!persist_destroyed);
    assert(persist_destroyed.error() ==
        os::keys::key_error(os::keys::errors::provider_failure));

    assert(restarted_provider.destroy(restored.value()));
    return 0;
}
