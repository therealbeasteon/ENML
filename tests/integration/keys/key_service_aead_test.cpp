#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <os/core/span.hpp>
#include <os/keys/ciphertext.hpp>
#include <os/keys/error.hpp>
#include <os/keys/key.hpp>
#include <os/keys/testing/openssl_provider.hpp>

namespace {

[[nodiscard]] os::core::ByteSpan as_bytes(std::string_view text) noexcept {
    return {
        reinterpret_cast<const std::byte*>(text.data()),
        text.size(),
    };
}

void copy_bytes(os::core::ByteSpan input, os::core::MutableByteSpan output) noexcept {
    assert(output.size() >= input.size());
    std::copy(input.begin(), input.end(), output.begin());
}

} // namespace

int main() {
    os::keys::testing::OpenSslTestKeyProvider provider;
    auto generated = provider.generate(os::keys::KeyPurpose::application_data_aead);
    assert(generated);
    const auto provider_key = generated.value();

    constexpr os::keys::KeyId key_id{
        0x4145414454455354ULL,
        1U,
    };
    constexpr std::uint32_t key_version = 1U;
    constexpr std::string_view plaintext_text = "ENML private storage authenticated payload";
    constexpr std::string_view aad_text = "principal-profile:50;object:data-v1";

    std::array<std::byte, os::keys::max_ciphertext_envelope_bytes> envelope{};
    const os::keys::CiphertextHeaderV1 header{
        .profile = os::keys::CryptoProfileId::aes_256_gcm_v1,
        .key_id = key_id,
        .key_version = key_version,
        .ciphertext_size = static_cast<std::uint32_t>(plaintext_text.size()),
    };
    auto encoded_header = os::keys::encode_ciphertext_header_v1(header, envelope);
    assert(encoded_header);
    assert(encoded_header.value() == os::keys::ciphertext_header_bytes);

    os::keys::AeadNonce nonce{};
    os::keys::AeadTag tag{};
    const auto ciphertext_offset = os::keys::ciphertext_fixed_overhead;
    auto sealed = provider.seal(
        provider_key,
        os::keys::CryptoProfileId::aes_256_gcm_v1,
        {envelope.data(), os::keys::ciphertext_header_bytes},
        as_bytes(aad_text),
        as_bytes(plaintext_text),
        {envelope.data() + static_cast<std::ptrdiff_t>(ciphertext_offset), plaintext_text.size()},
        nonce,
        tag);
    assert(sealed);
    assert(sealed.value() == plaintext_text.size());

    copy_bytes(
        {nonce.bytes.data(), nonce.bytes.size()},
        {envelope.data() + static_cast<std::ptrdiff_t>(os::keys::ciphertext_header_bytes), nonce.bytes.size()});
    copy_bytes(
        {tag.bytes.data(), tag.bytes.size()},
        {envelope.data() + static_cast<std::ptrdiff_t>(
            os::keys::ciphertext_header_bytes + os::keys::aead_nonce_bytes), tag.bytes.size()});

    const std::size_t envelope_size = os::keys::ciphertext_fixed_overhead + plaintext_text.size();
    const os::core::ByteSpan envelope_view{envelope.data(), envelope_size};
    auto parsed = os::keys::parse_ciphertext_envelope_v1(envelope_view);
    assert(parsed);
    assert(parsed.value().header.key_id == key_id);
    assert(parsed.value().header.key_version == key_version);
    assert(parsed.value().header.profile == os::keys::CryptoProfileId::aes_256_gcm_v1);
    assert(parsed.value().ciphertext.size() == plaintext_text.size());

    std::array<std::byte, os::keys::max_key_plaintext_bytes> plaintext_output{};
    auto opened = provider.open(
        provider_key,
        parsed.value().header.profile,
        parsed.value().authenticated_header,
        as_bytes(aad_text),
        nonce,
        tag,
        parsed.value().ciphertext,
        plaintext_output);
    assert(opened);
    assert(opened.value() == plaintext_text.size());
    assert(std::equal(
        plaintext_output.begin(),
        plaintext_output.begin() + static_cast<std::ptrdiff_t>(opened.value()),
        as_bytes(plaintext_text).begin()));

    auto wrong_aad = provider.open(
        provider_key,
        parsed.value().header.profile,
        parsed.value().authenticated_header,
        as_bytes("principal-profile:wrong"),
        nonce,
        tag,
        parsed.value().ciphertext,
        plaintext_output);
    assert(!wrong_aad);
    assert(wrong_aad.error() == os::keys::key_error(os::keys::errors::authentication_failed));

    auto tampered_tag = tag;
    tampered_tag.bytes[0] ^= std::byte{0x01};
    auto bad_tag = provider.open(
        provider_key,
        parsed.value().header.profile,
        parsed.value().authenticated_header,
        as_bytes(aad_text),
        nonce,
        tampered_tag,
        parsed.value().ciphertext,
        plaintext_output);
    assert(!bad_tag);
    assert(bad_tag.error() == os::keys::key_error(os::keys::errors::authentication_failed));

    auto tampered_envelope = envelope;
    tampered_envelope[ciphertext_offset] ^= std::byte{0x80};
    auto tampered_parsed = os::keys::parse_ciphertext_envelope_v1(
        {tampered_envelope.data(), envelope_size});
    assert(tampered_parsed);
    auto bad_ciphertext = provider.open(
        provider_key,
        tampered_parsed.value().header.profile,
        tampered_parsed.value().authenticated_header,
        as_bytes(aad_text),
        nonce,
        tag,
        tampered_parsed.value().ciphertext,
        plaintext_output);
    assert(!bad_ciphertext);
    assert(bad_ciphertext.error() == os::keys::key_error(os::keys::errors::authentication_failed));

    auto malformed_profile = envelope;
    malformed_profile[8] = std::byte{0x7F};
    auto bad_profile = os::keys::parse_ciphertext_envelope_v1(
        {malformed_profile.data(), envelope_size});
    assert(!bad_profile);
    assert(bad_profile.error() == os::keys::key_error(os::keys::errors::unsupported_crypto_profile));

    auto malformed_version = envelope;
    malformed_version[6] = std::byte{0x02};
    auto bad_envelope_version = os::keys::parse_ciphertext_envelope_v1(
        {malformed_version.data(), envelope_size});
    assert(!bad_envelope_version);
    assert(bad_envelope_version.error() == os::keys::key_error(os::keys::errors::malformed_ciphertext));

    std::array<std::byte, os::keys::max_key_plaintext_bytes> maximum_plaintext{};
    for (std::size_t index = 0U; index < maximum_plaintext.size(); ++index) {
        maximum_plaintext[index] = static_cast<std::byte>(index & 0xFFU);
    }
    std::array<std::byte, os::keys::max_key_aad_bytes> maximum_aad{};
    maximum_aad.fill(std::byte{0xA5});
    const os::keys::CiphertextHeaderV1 maximum_header{
        .profile = os::keys::CryptoProfileId::aes_256_gcm_v1,
        .key_id = key_id,
        .key_version = key_version,
        .ciphertext_size = static_cast<std::uint32_t>(maximum_plaintext.size()),
    };
    assert(os::keys::encode_ciphertext_header_v1(maximum_header, envelope));
    auto maximum_sealed = provider.seal(
        provider_key,
        maximum_header.profile,
        {envelope.data(), os::keys::ciphertext_header_bytes},
        maximum_aad,
        maximum_plaintext,
        {envelope.data() + static_cast<std::ptrdiff_t>(ciphertext_offset), maximum_plaintext.size()},
        nonce,
        tag);
    assert(maximum_sealed);
    assert(maximum_sealed.value() == maximum_plaintext.size());

    std::array<std::byte, os::keys::max_key_plaintext_bytes + 1U> oversized{};
    auto too_large = provider.seal(
        provider_key,
        maximum_header.profile,
        {envelope.data(), os::keys::ciphertext_header_bytes},
        {},
        oversized,
        {envelope.data() + static_cast<std::ptrdiff_t>(ciphertext_offset), maximum_plaintext.size()},
        nonce,
        tag);
    assert(!too_large);
    assert(too_large.error() == os::keys::key_error(os::keys::errors::too_large));

    assert(provider.destroy(provider_key));
    auto after_destroy = provider.open(
        provider_key,
        os::keys::CryptoProfileId::aes_256_gcm_v1,
        parsed.value().authenticated_header,
        as_bytes(aad_text),
        nonce,
        tag,
        parsed.value().ciphertext,
        plaintext_output);
    assert(!after_destroy);
    assert(after_destroy.error() == os::keys::key_error(os::keys::errors::provider_failure));

    return 0;
}
