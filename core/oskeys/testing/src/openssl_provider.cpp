#include <os/keys/testing/openssl_provider.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <os/keys/error.hpp>

namespace os::keys::testing {
namespace {

[[nodiscard]] constexpr bool fits_openssl_int(std::size_t size) noexcept {
    return size <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

void cleanse(os::core::MutableByteSpan bytes) noexcept {
    if (!bytes.empty()) {
        OPENSSL_cleanse(bytes.data(), bytes.size());
    }
}

[[nodiscard]] bool add_encrypt_aad(
    EVP_CIPHER_CTX* context,
    os::core::ByteSpan aad) noexcept {
    if (aad.empty()) return true;
    if (!fits_openssl_int(aad.size())) return false;
    int produced = 0;
    return EVP_EncryptUpdate(
        context,
        nullptr,
        &produced,
        reinterpret_cast<const unsigned char*>(aad.data()),
        static_cast<int>(aad.size())) == 1;
}

[[nodiscard]] bool add_decrypt_aad(
    EVP_CIPHER_CTX* context,
    os::core::ByteSpan aad) noexcept {
    if (aad.empty()) return true;
    if (!fits_openssl_int(aad.size())) return false;
    int produced = 0;
    return EVP_DecryptUpdate(
        context,
        nullptr,
        &produced,
        reinterpret_cast<const unsigned char*>(aad.data()),
        static_cast<int>(aad.size())) == 1;
}

} // namespace

OpenSslTestKeyProvider::~OpenSslTestKeyProvider() {
    for (auto& slot : slots_) {
        OPENSSL_cleanse(slot.key.data(), slot.key.size());
        slot.occupied = false;
    }
}

ProviderKeyReference OpenSslTestKeyProvider::make_reference(
    std::size_t index,
    std::uint32_t generation) noexcept {
    const auto token = static_cast<std::uint64_t>(index + 1U);
    return ProviderKeyReference{
        (static_cast<std::uint64_t>(generation) << 32U) | token,
    };
}

OpenSslTestKeyProvider::Slot*
OpenSslTestKeyProvider::resolve(ProviderKeyReference reference) noexcept {
    if (!reference.valid()) return nullptr;
    const std::uint32_t token = static_cast<std::uint32_t>(reference.value & 0xFFFFFFFFULL);
    const std::uint32_t generation = static_cast<std::uint32_t>(reference.value >> 32U);
    if (token == 0U || generation == 0U ||
        static_cast<std::size_t>(token) > slots_.size()) {
        return nullptr;
    }
    auto& slot = slots_[static_cast<std::size_t>(token - 1U)];
    if (!slot.occupied || slot.generation != generation) return nullptr;
    return &slot;
}

const OpenSslTestKeyProvider::Slot*
OpenSslTestKeyProvider::resolve(ProviderKeyReference reference) const noexcept {
    if (!reference.valid()) return nullptr;
    const std::uint32_t token = static_cast<std::uint32_t>(reference.value & 0xFFFFFFFFULL);
    const std::uint32_t generation = static_cast<std::uint32_t>(reference.value >> 32U);
    if (token == 0U || generation == 0U ||
        static_cast<std::size_t>(token) > slots_.size()) {
        return nullptr;
    }
    const auto& slot = slots_[static_cast<std::size_t>(token - 1U)];
    if (!slot.occupied || slot.generation != generation) return nullptr;
    return &slot;
}

os::core::Result<ProviderKeyReference>
OpenSslTestKeyProvider::generate(KeyPurpose purpose) noexcept {
    if (purpose != KeyPurpose::application_data_aead) {
        return key_error(errors::unsupported_purpose);
    }

    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        auto& slot = slots_[index];
        if (slot.occupied || slot.generation == std::numeric_limits<std::uint32_t>::max()) {
            continue;
        }

        std::array<std::byte, key_bytes> candidate{};
        if (RAND_bytes(
                reinterpret_cast<unsigned char*>(candidate.data()),
                static_cast<int>(candidate.size())) != 1) {
            OPENSSL_cleanse(candidate.data(), candidate.size());
            return key_error(errors::provider_failure);
        }

        ++slot.generation;
        slot.key = candidate;
        slot.occupied = true;
        OPENSSL_cleanse(candidate.data(), candidate.size());
        return make_reference(index, slot.generation);
    }
    return key_error(errors::registry_full);
}

os::core::Result<std::size_t>
OpenSslTestKeyProvider::seal(
    ProviderKeyReference key,
    CryptoProfileId profile,
    os::core::ByteSpan envelope_aad,
    os::core::ByteSpan caller_aad,
    os::core::ByteSpan plaintext,
    os::core::MutableByteSpan ciphertext,
    AeadNonce& nonce,
    AeadTag& tag) noexcept {
    if (profile != CryptoProfileId::aes_256_gcm_v1) {
        return key_error(errors::unsupported_crypto_profile);
    }
    const auto* slot = resolve(key);
    if (slot == nullptr) return key_error(errors::provider_failure);
    if (plaintext.size() > max_key_plaintext_bytes || !fits_openssl_int(plaintext.size())) {
        return key_error(errors::too_large);
    }
    if (ciphertext.size() < plaintext.size()) {
        return key_error(errors::output_too_small);
    }

    if (RAND_bytes(
            reinterpret_cast<unsigned char*>(nonce.bytes.data()),
            static_cast<int>(nonce.bytes.size())) != 1) {
        return key_error(errors::provider_failure);
    }

    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if (context == nullptr) return key_error(errors::provider_failure);

    bool ok = EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1;
    ok = ok && EVP_CIPHER_CTX_ctrl(
        context,
        EVP_CTRL_GCM_SET_IVLEN,
        static_cast<int>(nonce.bytes.size()),
        nullptr) == 1;
    ok = ok && EVP_EncryptInit_ex(
        context,
        nullptr,
        nullptr,
        reinterpret_cast<const unsigned char*>(slot->key.data()),
        reinterpret_cast<const unsigned char*>(nonce.bytes.data())) == 1;
    ok = ok && add_encrypt_aad(context, envelope_aad);
    ok = ok && add_encrypt_aad(context, caller_aad);

    int produced = 0;
    std::size_t total = 0U;
    if (ok && !plaintext.empty()) {
        ok = EVP_EncryptUpdate(
            context,
            reinterpret_cast<unsigned char*>(ciphertext.data()),
            &produced,
            reinterpret_cast<const unsigned char*>(plaintext.data()),
            static_cast<int>(plaintext.size())) == 1;
        if (ok) total = static_cast<std::size_t>(produced);
    }

    int final_bytes = 0;
    if (ok) {
        ok = EVP_EncryptFinal_ex(
            context,
            reinterpret_cast<unsigned char*>(ciphertext.data()) + total,
            &final_bytes) == 1;
        if (ok) total += static_cast<std::size_t>(final_bytes);
    }
    if (ok) {
        ok = EVP_CIPHER_CTX_ctrl(
            context,
            EVP_CTRL_GCM_GET_TAG,
            static_cast<int>(tag.bytes.size()),
            tag.bytes.data()) == 1;
    }
    EVP_CIPHER_CTX_free(context);

    if (!ok || total != plaintext.size()) {
        cleanse(ciphertext.first(plaintext.size()));
        OPENSSL_cleanse(nonce.bytes.data(), nonce.bytes.size());
        OPENSSL_cleanse(tag.bytes.data(), tag.bytes.size());
        return key_error(errors::provider_failure);
    }
    return total;
}

os::core::Result<std::size_t>
OpenSslTestKeyProvider::open(
    ProviderKeyReference key,
    CryptoProfileId profile,
    os::core::ByteSpan envelope_aad,
    os::core::ByteSpan caller_aad,
    const AeadNonce& nonce,
    const AeadTag& tag,
    os::core::ByteSpan ciphertext,
    os::core::MutableByteSpan plaintext) noexcept {
    if (profile != CryptoProfileId::aes_256_gcm_v1) {
        return key_error(errors::unsupported_crypto_profile);
    }
    const auto* slot = resolve(key);
    if (slot == nullptr) return key_error(errors::provider_failure);
    if (ciphertext.size() > max_key_plaintext_bytes || !fits_openssl_int(ciphertext.size())) {
        return key_error(errors::too_large);
    }
    if (plaintext.size() < ciphertext.size()) {
        return key_error(errors::output_too_small);
    }

    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if (context == nullptr) return key_error(errors::provider_failure);

    bool ok = EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1;
    ok = ok && EVP_CIPHER_CTX_ctrl(
        context,
        EVP_CTRL_GCM_SET_IVLEN,
        static_cast<int>(nonce.bytes.size()),
        nullptr) == 1;
    ok = ok && EVP_DecryptInit_ex(
        context,
        nullptr,
        nullptr,
        reinterpret_cast<const unsigned char*>(slot->key.data()),
        reinterpret_cast<const unsigned char*>(nonce.bytes.data())) == 1;
    ok = ok && add_decrypt_aad(context, envelope_aad);
    ok = ok && add_decrypt_aad(context, caller_aad);

    int produced = 0;
    std::size_t total = 0U;
    if (ok && !ciphertext.empty()) {
        ok = EVP_DecryptUpdate(
            context,
            reinterpret_cast<unsigned char*>(plaintext.data()),
            &produced,
            reinterpret_cast<const unsigned char*>(ciphertext.data()),
            static_cast<int>(ciphertext.size())) == 1;
        if (ok) total = static_cast<std::size_t>(produced);
    }

    std::array<std::byte, aead_tag_bytes> tag_copy = tag.bytes;
    if (ok) {
        ok = EVP_CIPHER_CTX_ctrl(
            context,
            EVP_CTRL_GCM_SET_TAG,
            static_cast<int>(tag_copy.size()),
            tag_copy.data()) == 1;
    }

    int final_bytes = 0;
    const int final_result = ok
        ? EVP_DecryptFinal_ex(
            context,
            reinterpret_cast<unsigned char*>(plaintext.data()) + total,
            &final_bytes)
        : 0;
    EVP_CIPHER_CTX_free(context);
    OPENSSL_cleanse(tag_copy.data(), tag_copy.size());

    if (!ok) {
        cleanse(plaintext.first(ciphertext.size()));
        return key_error(errors::provider_failure);
    }
    if (final_result != 1) {
        cleanse(plaintext.first(ciphertext.size()));
        return key_error(errors::authentication_failed);
    }
    total += static_cast<std::size_t>(final_bytes);
    if (total != ciphertext.size()) {
        cleanse(plaintext.first(ciphertext.size()));
        return key_error(errors::provider_failure);
    }
    return total;
}

os::core::Result<void>
OpenSslTestKeyProvider::destroy(ProviderKeyReference key) noexcept {
    auto* slot = resolve(key);
    if (slot == nullptr) return key_error(errors::provider_failure);
    OPENSSL_cleanse(slot->key.data(), slot->key.size());
    slot->occupied = false;
    return {};
}

} // namespace os::keys::testing
