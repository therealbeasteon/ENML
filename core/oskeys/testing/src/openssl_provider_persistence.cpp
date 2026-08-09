#include <os/keys/testing/openssl_provider.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <os/keys/error.hpp>

namespace os::keys::testing {
namespace {

inline constexpr std::uint32_t persistent_blob_magic = 0x3242504BU; // "KPB2" LE
inline constexpr std::uint16_t persistent_blob_version = 2U;
inline constexpr std::uint16_t persistent_blob_header_size = 12U;
inline constexpr std::size_t persistent_blob_nonce_offset = 12U;
inline constexpr std::size_t persistent_blob_ciphertext_offset =
    persistent_blob_nonce_offset + aead_nonce_bytes;
inline constexpr std::size_t persistent_blob_tag_offset =
    persistent_blob_ciphertext_offset + 32U;
inline constexpr std::size_t persistent_blob_size =
    persistent_blob_tag_offset + aead_tag_bytes;

// CI-only fixed wrapping root. This provides deterministic restart semantics
// for tests only. Production must use a hardware-backed provider root.
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
    const auto low = std::to_integer<std::uint32_t>(input[0]);
    const auto high = std::to_integer<std::uint32_t>(input[1]);
    return static_cast<std::uint16_t>(low | (high << 8U));
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
    if (aad.size() > static_cast<std::size_t>(INT_MAX)) return false;
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
    if (aad.size() > static_cast<std::size_t>(INT_MAX)) return false;
    int produced = 0;
    return EVP_DecryptUpdate(
        context,
        nullptr,
        &produced,
        reinterpret_cast<const unsigned char*>(aad.data()),
        static_cast<int>(aad.size())) == 1;
}

} // namespace

os::core::Result<std::size_t>
OpenSslTestKeyProvider::persist_reference(
    ProviderKeyReference key,
    KeyPurpose purpose,
    os::core::ByteSpan binding,
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
    ok = ok && add_encrypt_aad(context, binding);

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
        OPENSSL_cleanse(output.data(), persistent_blob_size);
        return key_error(errors::provider_failure);
    }
    return persistent_blob_size;
}

os::core::Result<ProviderKeyReference>
OpenSslTestKeyProvider::restore_reference(
    KeyPurpose purpose,
    os::core::ByteSpan binding,
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
    ok = ok && add_decrypt_aad(context, binding);

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
