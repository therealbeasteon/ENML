#include <os/storage/protected_namespace_snapshot.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace os::storage {
namespace {

void write_u16_le(os::core::MutableByteSpan out, std::size_t offset, std::uint16_t value) noexcept {
    out[offset] = static_cast<std::byte>(value & 0xFFU);
    out[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void write_u32_le(os::core::MutableByteSpan out, std::size_t offset, std::uint32_t value) noexcept {
    for (std::size_t i = 0U; i < 4U; ++i) {
        out[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
    }
}

void write_u64_le(os::core::MutableByteSpan out, std::size_t offset, std::uint64_t value) noexcept {
    for (std::size_t i = 0U; i < 8U; ++i) {
        out[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
    }
}

std::uint16_t read_u16_le(os::core::ByteSpan in, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint16_t>(in[offset]) |
        (std::to_integer<std::uint16_t>(in[offset + 1U]) << 8U));
}

std::uint32_t read_u32_le(os::core::ByteSpan in, std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    for (std::size_t i = 0U; i < 4U; ++i) {
        value |= std::to_integer<std::uint32_t>(in[offset + i]) << (8U * i);
    }
    return value;
}

std::uint64_t read_u64_le(os::core::ByteSpan in, std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t i = 0U; i < 8U; ++i) {
        value |= std::to_integer<std::uint64_t>(in[offset + i]) << (8U * i);
    }
    return value;
}

os::core::Result<void> encode_header(
    const ProtectedNamespaceSnapshotHeaderV1& header,
    os::core::MutableByteSpan out) noexcept {
    if (!header.valid() || out.size() < protected_namespace_snapshot_header_bytes) {
        return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::invalid);
    }
    write_u32_le(out, 0U, protected_namespace_snapshot_magic);
    write_u16_le(out, 4U, protected_namespace_snapshot_version);
    write_u16_le(out, 6U, protected_namespace_snapshot_header_bytes);
    write_u64_le(out, 8U, header.user.value());
    write_u64_le(out, 16U, header.security_epoch.value);
    write_u64_le(out, 24U, header.sequence);
    write_u32_le(out, 32U, header.entry_count);
    write_u32_le(out, 36U, header.flags);
    return {};
}

os::core::Result<ProtectedNamespaceSnapshotHeaderV1> decode_header(os::core::ByteSpan in) noexcept {
    if (in.size() != protected_namespace_snapshot_header_bytes ||
        read_u32_le(in, 0U) != protected_namespace_snapshot_magic ||
        read_u16_le(in, 4U) != protected_namespace_snapshot_version ||
        read_u16_le(in, 6U) != protected_namespace_snapshot_header_bytes) {
        return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::invalid);
    }
    ProtectedNamespaceSnapshotHeaderV1 header{
        .user = os::core::UserId{read_u64_le(in, 8U)},
        .security_epoch = os::keys::SecurityEpoch{read_u64_le(in, 16U)},
        .sequence = read_u64_le(in, 24U),
        .entry_count = read_u32_le(in, 32U),
        .flags = read_u32_le(in, 36U),
    };
    if (!header.valid()) {
        return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::invalid);
    }
    return header;
}

os::core::Result<std::size_t> encode_entries(
    std::span<const ProtectedNamespaceEntry> entries,
    os::core::UserId expected_user,
    os::core::MutableByteSpan out) noexcept {
    std::size_t cursor = 0U;
    for (const auto& entry : entries) {
        if (!entry.valid() || entry.user != expected_user) {
            return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::malformed_entry);
        }
        const auto path = entry.path.view();
        if (path.empty() || path.size() > max_relative_path_bytes || path.size() > 0xFFFFU) {
            return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::malformed_entry);
        }
        const std::size_t required = 42U + path.size();
        if (cursor > out.size() || required > out.size() - cursor) {
            return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::too_large);
        }
        write_u64_le(out, cursor, entry.principal.high);
        write_u64_le(out, cursor + 8U, entry.principal.low);
        write_u64_le(out, cursor + 16U, entry.object_id.high);
        write_u64_le(out, cursor + 24U, entry.object_id.low);
        write_u64_le(out, cursor + 32U, entry.generation);
        write_u16_le(out, cursor + 40U, static_cast<std::uint16_t>(path.size()));
        for (std::size_t i = 0U; i < path.size(); ++i) {
            out[cursor + 42U + i] = static_cast<std::byte>(static_cast<unsigned char>(path[i]));
        }
        cursor += required;
    }
    return cursor;
}

os::core::Result<std::size_t> decode_entries(
    const ProtectedNamespaceSnapshotHeaderV1& header,
    os::core::ByteSpan plaintext,
    std::span<ProtectedNamespaceEntry> entries) noexcept {
    if (header.entry_count > entries.size()) {
        return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::too_large);
    }
    std::size_t cursor = 0U;
    for (std::size_t index = 0U; index < header.entry_count; ++index) {
        if (cursor > plaintext.size() || plaintext.size() - cursor < 42U) {
            return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::malformed_entry);
        }
        const auto path_size = static_cast<std::size_t>(read_u16_le(plaintext, cursor + 40U));
        if (path_size == 0U || path_size > max_relative_path_bytes ||
            path_size > plaintext.size() - cursor - 42U) {
            return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::malformed_entry);
        }
        std::array<char, max_relative_path_bytes> path_bytes{};
        for (std::size_t i = 0U; i < path_size; ++i) {
            path_bytes[i] = static_cast<char>(std::to_integer<unsigned char>(plaintext[cursor + 42U + i]));
        }
        auto path = RelativePath::parse(std::string_view(path_bytes.data(), path_size));
        if (!path) {
            return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::malformed_entry);
        }
        ProtectedNamespaceEntry entry{
            .principal = os::core::PrincipalId{
                read_u64_le(plaintext, cursor),
                read_u64_le(plaintext, cursor + 8U)},
            .user = header.user,
            .path = path.value(),
            .object_id = ProtectedObjectId{
                read_u64_le(plaintext, cursor + 16U),
                read_u64_le(plaintext, cursor + 24U)},
            .generation = read_u64_le(plaintext, cursor + 32U),
        };
        if (!entry.valid()) {
            return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::malformed_entry);
        }
        entries[index] = entry;
        cursor += 42U + path_size;
    }
    if (cursor != plaintext.size()) {
        return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::malformed_entry);
    }
    return static_cast<std::size_t>(header.entry_count);
}

} // namespace

os::core::Result<std::size_t>
ProtectedNamespaceSnapshotCrypto::seal(
    os::keys::ProviderKeyReference metadata_key,
    const ProtectedNamespaceSnapshotHeaderV1& requested_header,
    const ProtectedNamespaceRegistry& registry,
    os::core::MutableByteSpan plaintext_scratch,
    os::core::MutableByteSpan output) noexcept {
    if (provider_ == nullptr || !metadata_key.valid() || !requested_header.valid() ||
        plaintext_scratch.size() < max_protected_namespace_snapshot_plaintext_bytes ||
        output.size() < protected_namespace_snapshot_overhead_bytes) {
        return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::invalid);
    }

    std::array<ProtectedNamespaceEntry, max_protected_namespace_entries> entries{};
    auto copied = registry.copy_user_entries(requested_header.user, entries);
    if (!copied) return copied.error();
    if (copied.value() != requested_header.entry_count) {
        return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::invalid);
    }

    auto plaintext_size = encode_entries(
        std::span<const ProtectedNamespaceEntry>{entries.data(), copied.value()},
        requested_header.user,
        plaintext_scratch.first(max_protected_namespace_snapshot_plaintext_bytes));
    if (!plaintext_size) return plaintext_size.error();

    const std::size_t required = protected_namespace_snapshot_overhead_bytes + plaintext_size.value();
    if (output.size() < required) {
        return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::too_large);
    }
    auto header_bytes = output.first(protected_namespace_snapshot_header_bytes);
    auto encoded_header = encode_header(requested_header, header_bytes);
    if (!encoded_header) return encoded_header.error();

    os::keys::AeadNonce nonce{};
    os::keys::AeadTag tag{};
    auto ciphertext = output.subspan(
        protected_namespace_snapshot_overhead_bytes,
        plaintext_size.value());
    auto sealed = provider_->seal(
        metadata_key,
        os::keys::CryptoProfileId::aes_256_gcm_v1,
        header_bytes,
        {},
        plaintext_scratch.first(plaintext_size.value()),
        ciphertext,
        nonce,
        tag);
    if (!sealed || sealed.value() != plaintext_size.value()) {
        return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::provider_failure);
    }
    std::copy(
        nonce.bytes.begin(), nonce.bytes.end(),
        output.begin() + static_cast<std::ptrdiff_t>(protected_namespace_snapshot_header_bytes));
    std::copy(
        tag.bytes.begin(), tag.bytes.end(),
        output.begin() + static_cast<std::ptrdiff_t>(protected_namespace_snapshot_header_bytes + os::keys::aead_nonce_bytes));
    return required;
}

os::core::Result<ProtectedNamespaceSnapshotHeaderV1>
ProtectedNamespaceSnapshotCrypto::open_and_restore(
    os::keys::ProviderKeyReference metadata_key,
    const ProtectedNamespaceFreshnessEvidence& freshness,
    os::core::ByteSpan record,
    os::core::MutableByteSpan plaintext_scratch,
    ProtectedNamespaceRegistry& registry) noexcept {
    if (provider_ == nullptr || !metadata_key.valid() ||
        record.size() < protected_namespace_snapshot_overhead_bytes ||
        record.size() > max_protected_namespace_snapshot_record_bytes ||
        plaintext_scratch.size() < max_protected_namespace_snapshot_plaintext_bytes) {
        return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::invalid);
    }

    auto header = decode_header(record.first(protected_namespace_snapshot_header_bytes));
    if (!header) return header.error();
    auto freshness_result = validate_namespace_snapshot_freshness(header.value(), freshness);
    if (!freshness_result) return freshness_result.error();

    os::keys::AeadNonce nonce{};
    os::keys::AeadTag tag{};
    const auto nonce_bytes = record.subspan(
        protected_namespace_snapshot_header_bytes,
        os::keys::aead_nonce_bytes);
    const auto tag_bytes = record.subspan(
        protected_namespace_snapshot_header_bytes + os::keys::aead_nonce_bytes,
        os::keys::aead_tag_bytes);
    std::copy(nonce_bytes.begin(), nonce_bytes.end(), nonce.bytes.begin());
    std::copy(tag_bytes.begin(), tag_bytes.end(), tag.bytes.begin());

    const auto ciphertext = record.subspan(protected_namespace_snapshot_overhead_bytes);
    if (ciphertext.size() > max_protected_namespace_snapshot_plaintext_bytes) {
        return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::too_large);
    }
    auto opened = provider_->open(
        metadata_key,
        os::keys::CryptoProfileId::aes_256_gcm_v1,
        record.first(protected_namespace_snapshot_header_bytes),
        {},
        nonce,
        tag,
        ciphertext,
        plaintext_scratch.first(ciphertext.size()));
    if (!opened || opened.value() != ciphertext.size()) {
        return protected_namespace_snapshot_error(protected_namespace_snapshot_errors::provider_failure);
    }

    std::array<ProtectedNamespaceEntry, max_protected_namespace_entries> entries{};
    auto decoded = decode_entries(
        header.value(),
        plaintext_scratch.first(opened.value()),
        entries);
    if (!decoded) return decoded.error();

    // replace_user_entries performs complete duplicate/capacity validation before
    // mutating live trusted state, so authentication or parsing failure cannot
    // partially reconstruct the namespace.
    auto replaced = registry.replace_user_entries(
        header.value().user,
        std::span<const ProtectedNamespaceEntry>{entries.data(), decoded.value()});
    if (!replaced) return replaced.error();
    return header.value();
}

} // namespace os::storage
