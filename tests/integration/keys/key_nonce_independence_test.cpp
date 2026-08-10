#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include <os/core/span.hpp>
#include <os/keys/ciphertext.hpp>
#include <os/keys/error.hpp>
#include <os/keys/key.hpp>
#include <os/keys/testing/openssl_provider.hpp>

// The AEAD nonce is provider-owned and never caller-influenced.
//
// This is the failure mode that broke Samsung's KeyMaster (CVE-2021-25444).
// There the IV reached AES-GCM from the normal world, so an attacker could
// force two wrappings under the same key and IV and recover an unknown key by
// XOR alone:
//
//     B_A xor B_B xor K_B = (E(HDK,IV) xor K_A) xor (E(HDK,IV) xor K_B) xor K_B
//                         = K_A
//
// No memory-safety bug is involved. The whole attack is keystream reuse caused
// by letting an untrusted caller pick the nonce. ENML's KeyStore::seal takes the
// nonce by non-const reference, which reads like an output parameter but is
// indistinguishable at the call site from an input one - so the guarantee is
// worth testing rather than assuming, especially before a production TPM/TEE
// provider is written against the same interface.
//
// Asserted here:
//   1. a caller-supplied nonce value is discarded, not used;
//   2. repeated seals of identical input never repeat a nonce;
//   3. repeated seals of identical input never repeat a ciphertext, which is
//      the observable consequence that made the Samsung XOR recovery possible.

namespace {

constexpr std::size_t seal_rounds = 8U;

// Deliberately not assert(): a security property must be enforced identically
// in every build configuration.
[[nodiscard]] bool check(bool condition, const char* what) noexcept {
    if (!condition) {
        std::fprintf(stderr, "nonce independence violated: %s\n", what);
    }
    return condition;
}

[[nodiscard]] os::core::ByteSpan as_bytes(std::string_view text) noexcept {
    return {
        reinterpret_cast<const std::byte*>(text.data()),
        text.size(),
    };
}

} // namespace

int main() {
    os::keys::testing::OpenSslTestKeyProvider provider;
    auto generated = provider.generate(os::keys::KeyPurpose::application_data_aead);
    if (!check(static_cast<bool>(generated), "provider could not generate a key")) {
        return 1;
    }
    const auto provider_key = generated.value();

    constexpr os::keys::KeyId key_id{0x4E4F4E4345303031ULL, 1U};
    constexpr std::uint32_t key_version = 1U;
    constexpr std::string_view plaintext_text = "identical plaintext, sealed repeatedly";
    constexpr std::string_view aad_text = "identical aad";

    // The value an attacker would try to pin the nonce to. Every seal below
    // starts from this exact buffer content.
    constexpr std::byte chosen_nonce_byte{0xAA};

    std::array<os::keys::AeadNonce, seal_rounds> nonces{};
    std::array<std::array<std::byte, os::keys::max_ciphertext_envelope_bytes>, seal_rounds>
        ciphertexts{};

    for (std::size_t round = 0U; round < seal_rounds; ++round) {
        std::array<std::byte, os::keys::max_ciphertext_envelope_bytes> envelope{};
        const os::keys::CiphertextHeaderV1 header{
            .profile = os::keys::CryptoProfileId::aes_256_gcm_v1,
            .key_id = key_id,
            .key_version = key_version,
            .ciphertext_size = static_cast<std::uint32_t>(plaintext_text.size()),
        };
        auto encoded_header = os::keys::encode_ciphertext_header_v1(header, envelope);
        if (!check(static_cast<bool>(encoded_header), "header encode failed")) {
            return 1;
        }

        os::keys::AeadNonce nonce{};
        for (auto& byte : nonce.bytes) {
            byte = chosen_nonce_byte;
        }
        os::keys::AeadTag tag{};

        const auto ciphertext_offset = os::keys::ciphertext_fixed_overhead;
        auto sealed = provider.seal(
            provider_key,
            os::keys::CryptoProfileId::aes_256_gcm_v1,
            {envelope.data(), os::keys::ciphertext_header_bytes},
            as_bytes(aad_text),
            as_bytes(plaintext_text),
            {envelope.data() + static_cast<std::ptrdiff_t>(ciphertext_offset),
             plaintext_text.size()},
            nonce,
            tag);
        if (!check(static_cast<bool>(sealed), "seal failed")) {
            return 1;
        }

        // 1. The caller's chosen value must not survive into the used nonce.
        bool all_chosen = true;
        for (const auto byte : nonce.bytes) {
            if (byte != chosen_nonce_byte) {
                all_chosen = false;
                break;
            }
        }
        if (!check(!all_chosen, "provider used the caller-supplied nonce")) {
            return 1;
        }

        nonces[round] = nonce;
        ciphertexts[round] = envelope;
    }

    // 2. and 3. No nonce and no ciphertext may repeat across identical inputs.
    for (std::size_t left = 0U; left < seal_rounds; ++left) {
        for (std::size_t right = left + 1U; right < seal_rounds; ++right) {
            bool same_nonce = true;
            for (std::size_t index = 0U; index < nonces[left].bytes.size(); ++index) {
                if (nonces[left].bytes[index] != nonces[right].bytes[index]) {
                    same_nonce = false;
                    break;
                }
            }
            if (!check(!same_nonce, "two seals reused a nonce")) {
                return 1;
            }

            bool same_ciphertext = true;
            for (std::size_t index = 0U; index < ciphertexts[left].size(); ++index) {
                if (ciphertexts[left][index] != ciphertexts[right][index]) {
                    same_ciphertext = false;
                    break;
                }
            }
            if (!check(!same_ciphertext, "two seals produced identical ciphertext")) {
                return 1;
            }
        }
    }

    return 0;
}
