#include <os/storage/protected_key_record.hpp>

#include <cstddef>
#include <cstdint>

#include <os/storage/error.hpp>

namespace os::storage {
namespace {

void write_u16_le(os::core::MutableByteSpan out, std::size_t offset, std::uint16_t value) noexcept {
    out[offset] = static_cast<std::byte>(value & 0xFFU);
    out[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}
void write_u32_le(os::core::MutableByteSpan out, std::size_t offset, std::uint32_t value) noexcept {
    for (std::size_t i = 0U; i < 4U; ++i) out[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
}
void write_u64_le(os::core::MutableByteSpan out, std::size_t offset, std::uint64_t value) noexcept {
    for (std::size_t i = 0U; i < 8U; ++i) out[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
}
[[nodiscard]] std::uint16_t read_u16_le(os::core::ByteSpan in, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(in[offset]) |
        (std::to_integer<std::uint16_t>(in[offset + 1U]) << 8U));
}
[[nodiscard]] std::uint32_t read_u32_le(os::core::ByteSpan in, std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    for (std::size_t i = 0U; i < 4U; ++i) value |= std::to_integer<std::uint32_t>(in[offset + i]) << (8U * i);
    return value;
}
[[nodiscard]] std::uint64_t read_u64_le(os::core::ByteSpan in, std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t i = 0U; i < 8U; ++i) value |= std::to_integer<std::uint64_t>(in[offset + i]) << (8U * i);
    return value;
}

[[nodiscard]] bool same_binding(
    const ProtectedObjectKeyBinding& left,
    const ProtectedObjectKeyBinding& right) noexcept {
    return left.principal == right.principal && left.user == right.user &&
        left.object_id == right.object_id && left.generation == right.generation;
}

void encode_binding(const ProtectedObjectKeyBinding& binding, os::core::MutableByteSpan out) noexcept {
    write_u32_le(out, 0U, protected_key_record_magic);
    write_u16_le(out, 4U, protected_key_record_version);
    write_u16_le(out, 6U, protected_key_record_header_bytes);
    write_u64_le(out, 8U, binding.user.value());
    write_u64_le(out, 16U, binding.principal.high);
    write_u64_le(out, 24U, binding.principal.low);
    write_u64_le(out, 32U, binding.object_id.high);
    write_u64_le(out, 40U, binding.object_id.low);
    write_u32_le(out, 48U, static_cast<std::uint32_t>(os::keys::KeyPurpose::profile_storage_aead));
    write_u32_le(out, 52U, binding.generation);
}

} // namespace

os::core::Result<std::size_t>
persist_protected_object_key_v1(
    os::keys::PersistentKeyProvider& provider,
    os::keys::ProviderKeyReference key,
    const ProtectedObjectKeyBinding& binding,
    os::core::MutableByteSpan output) noexcept {
    if (!key.valid() || !binding.valid()) return storage_error(errors::invalid_options);
    if (output.size() < protected_key_record_header_bytes + 1U) return storage_error(errors::too_large);

    encode_binding(binding, output.first(protected_key_record_header_bytes));
    write_u32_le(output, 56U, 0U);
    write_u32_le(output, 60U, 0U);

    auto persisted = provider.persist_reference(
        key,
        os::keys::KeyPurpose::profile_storage_aead,
        output.first(protected_key_binding_bytes),
        output.subspan(protected_key_record_header_bytes));
    if (!persisted) return persisted.error();
    if (persisted.value() == 0U || persisted.value() > os::keys::max_persistent_provider_blob_bytes) {
        return storage_error(errors::io_failure);
    }
    write_u32_le(output, 56U, static_cast<std::uint32_t>(persisted.value()));
    return static_cast<std::size_t>(protected_key_record_header_bytes) + persisted.value();
}

os::core::Result<ProtectedObjectKeyRecordView>
parse_protected_object_key_record_v1(os::core::ByteSpan record) noexcept {
    if (record.size() <= protected_key_record_header_bytes || record.size() > max_protected_key_record_bytes) {
        return storage_error(errors::invalid_options);
    }
    if (read_u32_le(record, 0U) != protected_key_record_magic ||
        read_u16_le(record, 4U) != protected_key_record_version ||
        read_u16_le(record, 6U) != protected_key_record_header_bytes ||
        read_u32_le(record, 48U) != static_cast<std::uint32_t>(os::keys::KeyPurpose::profile_storage_aead) ||
        read_u32_le(record, 60U) != 0U) {
        return storage_error(errors::invalid_options);
    }

    ProtectedObjectKeyBinding binding{
        .principal = os::core::PrincipalId{read_u64_le(record, 16U), read_u64_le(record, 24U)},
        .user = os::core::UserId{read_u64_le(record, 8U)},
        .object_id = ProtectedObjectId{read_u64_le(record, 32U), read_u64_le(record, 40U)},
        .generation = read_u32_le(record, 52U),
    };
    if (!binding.valid()) return storage_error(errors::invalid_options);

    const auto blob_size = read_u32_le(record, 56U);
    if (blob_size == 0U || blob_size > os::keys::max_persistent_provider_blob_bytes ||
        record.size() != protected_key_record_header_bytes + static_cast<std::size_t>(blob_size)) {
        return storage_error(errors::invalid_options);
    }
    return ProtectedObjectKeyRecordView{
        .binding = binding,
        .authenticated_binding = record.first(protected_key_binding_bytes),
        .provider_blob = record.subspan(protected_key_record_header_bytes, blob_size),
    };
}

os::core::Result<os::keys::ProviderKeyReference>
restore_protected_object_key_v1(
    os::keys::PersistentKeyProvider& provider,
    const ProtectedObjectKeyBinding& expected,
    os::core::ByteSpan record) noexcept {
    if (!expected.valid()) return storage_error(errors::invalid_options);
    auto parsed = parse_protected_object_key_record_v1(record);
    if (!parsed) return parsed.error();
    if (!same_binding(parsed.value().binding, expected)) return storage_error(errors::access_denied);
    return provider.restore_reference(
        os::keys::KeyPurpose::profile_storage_aead,
        parsed.value().authenticated_binding,
        parsed.value().provider_blob);
}

} // namespace os::storage
