#include <os/keys/testing/openssl_provider.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <os/keys/error.hpp>

namespace os::keys::testing {
namespace {

inline constexpr std::uint32_t persistent_blob_magic = 0x3142504BU; // "KPB1" LE
inline constexpr std::uint16_t persistent_blob_version = 1U;
inline constexpr std::uint16_t persistent_blob_header_size = 12U;
inline constexpr std::size_t persistent_blob_nonce_offset = 12U;
inline constexpr std::size_t persistent_blob_ciphertext_offset =
    persistent_blob_nonce_offset + aead_nonce_bytes;
inline constexpr std::size_t persistent_blob_tag_offset =
    persistent_blob_ciphertext_offset + 32U;
inline constexpr std::size_t persistent_blob_size =
    persistent_blob_tag_offset + aead_tag_bytes;

// CI-only wrapping key. Production providers must replace this entire mechanism
// with hardware-backed sealed objects or another provider-owned durable root.
inline constexpr std::array<std::byte, 32U> test_wrapping_key{
    std::byte{0x45}, std::byte{0x4E}, std::byte{0x4D}, std::byte{0x4C},
    std::byte{0x2D}, std::byte{0x43}, std::byte{0x49}, std::byte{0x2D},
    std::byte{0x57}, std::byte{0x52}, std::byte{0x41}, std::byte{0x50},
    std::byte{0x2D}, std::byte{0x4B}, std::byte{0x45}, std::byte{0x59},
    std::byte{0x2D}, std::byte{0x4E}, std::byte{0x4F}, std::byte{0x54},
    std::byte{0x2D}, std::byte{0x50}, std::byte{0x52}, std::byte{0x4F},
    std::byte{0x44}, std::byte{0x55}, std::byte{0x43}, std::byte{0x54},
    std::byte{0x49}, std::byte{0x4F}, std::byte{0x4E}, std::byte{0x21},
};

[[nodiscard]] constexpr bool fits_openssl_int(std::size_t size) noexcept {
    return size <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

void cleanse(os::core::MutableByteSpan bytes) noexcept {
    if (!bytes.empty()) {
        OPENSSL_cleanse(bytes.data(), bytes.size());
    }
}

void write_u16_le(std::byte* output, std::uint16_t value) noexcept {
    output[0] = static_cast<std::byte>(value & 0xFFU);
    output[1] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void write_u32_le(std::byte* output, std::uint32_t value) noexcept {
    output[0] = static_cast<std::byte>(value & 0xFFU);
    output[1] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    output[2] = static_cast<std::byte>((value >> 16U) & 0xFFU);
    output[3] = static_cast<std::byte>((value >> 24U) & 0xFFU);
}

[[nodiscard]] std::uint16_t read_u16_le(const std::byte* input) noexcept {
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint16_t>(input[0]) |
        static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(input[1]) << 8U));
}

[[nodiscard]] std::uint32_t read_u32_le(const std::byte* input) noexcept {
    return std::to_integer<std::uint32_t>(input[0]) |
        (std::to_integer<std::uint32_t>(input[1]) << 8U) |
        (std::to_integer<std::uint32_t>(input[2]) << 16U) |
        (std::to_integer<std::uint32_t>(input[3]) << 24U);
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
OpenSslTestKeyProvider::install_key(os::core::ByteSpan key_material) noexcept {
    if (key_material.size() != key_bytes) return key_error(errors::provider_failure);
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        auto& slot = slots_[index];
        if (slot.occupied || slot.generation == std::numeric_limits<std::uint32_t>::max()) {
            continue;
        }
        ++slot.generation;
        std::copy(key_material.begin(), key_material.end(), slot.key.begin());
        slot.occupied = true;
        return make_reference(index, slot.generation);
    }
    return key_error(errors::registry_full);
}

os::core::Result<ProviderKeyReference>
OpenSslTestKeyProvider::generate(KeyPurpose purpose) noexcept {
    // The flat entry point stays application-only. A profile storage key is
    // only meaningful beneath a user-profile root, and permitting one here
    // would let a caller mint it with no root and therefore no scope check -
    // which is the admission rule generate_under_root exists to apply.
    if (purpose != KeyPurpose::application_data_aead) {
        return key_error(errors::unsupported_purpose);
    }
    return generate_material();
}

os::core::Result<ProviderKeyReference>
OpenSslTestKeyProvider::generate_material() noexcept {
    std::array<std::byte, key_bytes> candidate{};
    if (RAND_bytes(
            reinterpret_cast<unsigned char*>(candidate.data()),
            static_cast<int>(candidate.size())) != 1) {
        OPENSSL_cleanse(candidate.data(), candidate.size());
        return key_error(errors::provider_failure);
    }
    auto installed = install_key(candidate);
    OPENSSL_cleanse(candidate.data(), candidate.size());
    return installed;
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

os::core::Result<std::size_t>
OpenSslTestKeyProvider::persist_reference(
    ProviderKeyReference key,
    KeyPurpose purpose,
    os::core::MutableByteSpan output) noexcept {
    if (!valid_purpose(purpose)) return key_error(errors::unsupported_purpose);
    const auto* slot = resolve(key);
    if (slot == nullptr) return key_error(errors::provider_failure);
    if (output.size() < persistent_blob_size) {
        return key_error(errors::output_too_small);
    }

    write_u32_le(output.data(), persistent_blob_magic);
    write_u16_le(output.data() + 4U, persistent_blob_version);
    write_u16_le(output.data() + 6U, persistent_blob_header_size);
    write_u32_le(output.data() + 8U, static_cast<std::uint32_t>(purpose));

    auto nonce = output.subspan(persistent_blob_nonce_offset, aead_nonce_bytes);
    auto ciphertext = output.subspan(persistent_blob_ciphertext_offset, key_bytes);
    auto tag = output.subspan(persistent_blob_tag_offset, aead_tag_bytes);
    if (RAND_bytes(
            reinterpret_cast<unsigned char*>(nonce.data()),
            static_cast<int>(nonce.size())) != 1) {
        return key_error(errors::provider_failure);
    }

    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if (context == nullptr) return key_error(errors::provider_failure);
    bool ok = EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1;
    ok = ok && EVP_CIPHER_CTX_ctrl(
        context, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) == 1;
    ok = ok && EVP_EncryptInit_ex(
        context,
        nullptr,
        nullptr,
        reinterpret_cast<const unsigned char*>(test_wrapping_key.data()),
        reinterpret_cast<const unsigned char*>(nonce.data())) == 1;
    ok = ok && add_encrypt_aad(context, output.first(persistent_blob_header_size));

    int produced = 0;
    if (ok) {
        ok = EVP_EncryptUpdate(
            context,
            reinterpret_cast<unsigned char*>(ciphertext.data()),
            &produced,
            reinterpret_cast<const unsigned char*>(slot->key.data()),
            static_cast<int>(slot->key.size())) == 1;
    }
    int final_bytes = 0;
    if (ok) {
        ok = EVP_EncryptFinal_ex(
            context,
            reinterpret_cast<unsigned char*>(ciphertext.data()) + produced,
            &final_bytes) == 1;
    }
    if (ok) {
        ok = EVP_CIPHER_CTX_ctrl(
            context,
            EVP_CTRL_GCM_GET_TAG,
            static_cast<int>(tag.size()),
            tag.data()) == 1;
    }
    EVP_CIPHER_CTX_free(context);

    if (!ok || static_cast<std::size_t>(produced + final_bytes) != key_bytes) {
        cleanse(output.first(persistent_blob_size));
        return key_error(errors::provider_failure);
    }
    return persistent_blob_size;
}

os::core::Result<ProviderKeyReference>
OpenSslTestKeyProvider::restore_reference(
    KeyPurpose purpose,
    os::core::ByteSpan persistent_blob) noexcept {
    if (!valid_purpose(purpose)) return key_error(errors::unsupported_purpose);
    if (persistent_blob.size() != persistent_blob_size ||
        read_u32_le(persistent_blob.data()) != persistent_blob_magic ||
        read_u16_le(persistent_blob.data() + 4U) != persistent_blob_version ||
        read_u16_le(persistent_blob.data() + 6U) != persistent_blob_header_size ||
        read_u32_le(persistent_blob.data() + 8U) != static_cast<std::uint32_t>(purpose)) {
        return key_error(errors::provider_failure);
    }

    const auto nonce = persistent_blob.subspan(persistent_blob_nonce_offset, aead_nonce_bytes);
    const auto ciphertext = persistent_blob.subspan(persistent_blob_ciphertext_offset, key_bytes);
    const auto tag = persistent_blob.subspan(persistent_blob_tag_offset, aead_tag_bytes);
    std::array<std::byte, key_bytes> key_material{};

    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    if (context == nullptr) return key_error(errors::provider_failure);
    bool ok = EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1;
    ok = ok && EVP_CIPHER_CTX_ctrl(
        context, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) == 1;
    ok = ok && EVP_DecryptInit_ex(
        context,
        nullptr,
        nullptr,
        reinterpret_cast<const unsigned char*>(test_wrapping_key.data()),
        reinterpret_cast<const unsigned char*>(nonce.data())) == 1;
    ok = ok && add_decrypt_aad(context, persistent_blob.first(persistent_blob_header_size));

    int produced = 0;
    if (ok) {
        ok = EVP_DecryptUpdate(
            context,
            reinterpret_cast<unsigned char*>(key_material.data()),
            &produced,
            reinterpret_cast<const unsigned char*>(ciphertext.data()),
            static_cast<int>(ciphertext.size())) == 1;
    }

    std::array<std::byte, aead_tag_bytes> tag_copy{};
    std::copy(tag.begin(), tag.end(), tag_copy.begin());
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
            reinterpret_cast<unsigned char*>(key_material.data()) + produced,
            &final_bytes)
        : 0;
    EVP_CIPHER_CTX_free(context);
    OPENSSL_cleanse(tag_copy.data(), tag_copy.size());

    if (!ok) {
        OPENSSL_cleanse(key_material.data(), key_material.size());
        return key_error(errors::provider_failure);
    }
    if (final_result != 1) {
        OPENSSL_cleanse(key_material.data(), key_material.size());
        return key_error(errors::authentication_failed);
    }
    if (static_cast<std::size_t>(produced + final_bytes) != key_bytes) {
        OPENSSL_cleanse(key_material.data(), key_material.size());
        return key_error(errors::provider_failure);
    }

    auto installed = install_key(key_material);
    OPENSSL_cleanse(key_material.data(), key_material.size());
    return installed;
}

} // namespace os::keys::testing
