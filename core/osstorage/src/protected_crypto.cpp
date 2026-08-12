#include <os/storage/protected_crypto.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <os/storage/error.hpp>

namespace os::storage {

os::core::Result<std::size_t>
ProtectedChunkCrypto::seal(
    os::keys::ProviderKeyReference key,
    const ProtectedChunkHeaderV2& header,
    os::core::ByteSpan plaintext,
    os::core::MutableByteSpan output) noexcept {
    if (provider_ == nullptr || !key.valid() || !valid_protected_chunk_header(header) ||
        header.plaintext_size != plaintext.size()) {
        return storage_error(errors::invalid_options);
    }
    if (plaintext.size() > protected_chunk_plaintext_bytes) return storage_error(errors::too_large);
    const std::size_t required = protected_chunk_overhead_bytes + plaintext.size();
    if (output.size() < required) return storage_error(errors::too_large);

    auto header_result = encode_protected_chunk_header_v2(
        header, output.first(protected_chunk_header_bytes));
    if (!header_result) return header_result.error();

    os::keys::AeadNonce nonce{};
    os::keys::AeadTag tag{};
    auto ciphertext = output.subspan(protected_chunk_overhead_bytes, plaintext.size());
    auto sealed = provider_->seal(
        key,
        header.crypto_profile,
        output.first(protected_chunk_header_bytes),
        {},
        plaintext,
        ciphertext,
        nonce,
        tag);
    if (!sealed) return sealed.error();
    if (sealed.value() != plaintext.size()) return storage_error(errors::io_failure);

    std::copy(nonce.bytes.begin(), nonce.bytes.end(),
        output.begin() + static_cast<std::ptrdiff_t>(protected_chunk_header_bytes));
    std::copy(tag.bytes.begin(), tag.bytes.end(),
        output.begin() + static_cast<std::ptrdiff_t>(protected_chunk_header_bytes + os::keys::aead_nonce_bytes));
    return required;
}

os::core::Result<std::size_t>
ProtectedChunkCrypto::open(
    os::keys::ProviderKeyReference key,
    const ProtectedChunkAddress& expected,
    os::core::ByteSpan record,
    os::core::MutableByteSpan plaintext) noexcept {
    if (provider_ == nullptr || !key.valid() || !expected.valid()) {
        return storage_error(errors::invalid_options);
    }
    if (record.size() < protected_chunk_overhead_bytes ||
        record.size() > max_protected_chunk_record_bytes) {
        return storage_error(errors::invalid_options);
    }

    auto decoded = decode_protected_chunk_header_v2(record.first(protected_chunk_header_bytes));
    if (!decoded) return decoded.error();
    const auto header = decoded.value();

    if (header.user != expected.user ||
        header.object_id != expected.object_id ||
        header.object_generation != expected.object_generation ||
        header.chunk_index != expected.chunk_index) {
        return storage_error(errors::access_denied);
    }

    const std::size_t expected_record_size =
        protected_chunk_overhead_bytes + static_cast<std::size_t>(header.plaintext_size);
    if (record.size() != expected_record_size || plaintext.size() < header.plaintext_size) {
        return storage_error(errors::invalid_options);
    }

    os::keys::AeadNonce nonce{};
    os::keys::AeadTag tag{};
    const auto nonce_bytes = record.subspan(protected_chunk_header_bytes, os::keys::aead_nonce_bytes);
    const auto tag_bytes = record.subspan(
        protected_chunk_header_bytes + os::keys::aead_nonce_bytes,
        os::keys::aead_tag_bytes);
    std::copy(nonce_bytes.begin(), nonce_bytes.end(), nonce.bytes.begin());
    std::copy(tag_bytes.begin(), tag_bytes.end(), tag.bytes.begin());

    const auto ciphertext = record.subspan(
        protected_chunk_overhead_bytes,
        static_cast<std::size_t>(header.plaintext_size));
    auto opened = provider_->open(
        key,
        header.crypto_profile,
        record.first(protected_chunk_header_bytes),
        {},
        nonce,
        tag,
        ciphertext,
        plaintext.first(header.plaintext_size));
    if (!opened) return opened.error();
    if (opened.value() != header.plaintext_size) return storage_error(errors::io_failure);
    return opened.value();
}

} // namespace os::storage
