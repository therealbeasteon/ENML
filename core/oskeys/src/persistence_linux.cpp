#include <os/keys/persistence.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <os/core/identity.hpp>
#include <os/keys/error.hpp>

namespace os::keys {
namespace {

constexpr const char* registry_file_name = "key-registry-v1.bin";
constexpr const char* registry_temp_file_name = ".key-registry-v1.tmp";
constexpr std::uint16_t record_flag_destroyed = 1U;
constexpr std::uint16_t record_known_flags = record_flag_destroyed;
constexpr std::size_t record_header_bytes = 60U;
constexpr std::size_t version_header_bytes = 8U;
constexpr std::uint32_t binding_magic_v1 = 0x3144424BU; // "KBD1" LE
constexpr std::uint16_t binding_version_v1 = 1U;

void write_u16(std::byte* output, std::uint16_t value) noexcept {
    output[0] = static_cast<std::byte>(value & 0x00FFU);
    output[1] = static_cast<std::byte>((value >> 8U) & 0x00FFU);
}

void write_u32(std::byte* output, std::uint32_t value) noexcept {
    for (unsigned index = 0U; index < 4U; ++index) {
        const unsigned shift = index * 8U;
        output[index] = static_cast<std::byte>((value >> shift) & 0x000000FFU);
    }
}

void write_u64(std::byte* output, std::uint64_t value) noexcept {
    for (unsigned index = 0U; index < 8U; ++index) {
        const unsigned shift = index * 8U;
        output[index] = static_cast<std::byte>((value >> shift) & 0x00000000000000FFULL);
    }
}

[[nodiscard]] std::uint16_t read_u16(const std::byte* input) noexcept {
    const auto low = static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(input[0]));
    const auto high = static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(input[1]));
    return static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8U));
}

[[nodiscard]] std::uint32_t read_u32(const std::byte* input) noexcept {
    std::uint32_t value = 0U;
    for (unsigned index = 0U; index < 4U; ++index) {
        const unsigned shift = index * 8U;
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(input[index])) << shift;
    }
    return value;
}

[[nodiscard]] std::uint64_t read_u64(const std::byte* input) noexcept {
    std::uint64_t value = 0U;
    for (unsigned index = 0U; index < 8U; ++index) {
        const unsigned shift = index * 8U;
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(input[index])) << shift;
    }
    return value;
}

[[nodiscard]] bool write_all(int fd, os::core::ByteSpan bytes) noexcept {
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto result = ::write(fd, bytes.data() + offset, bytes.size() - offset);
        if (result < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (result == 0) return false;
        offset += static_cast<std::size_t>(result);
    }
    return true;
}

[[nodiscard]] bool pwrite_all(int fd, os::core::ByteSpan bytes, off_t file_offset) noexcept {
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto result = ::pwrite(
            fd,
            bytes.data() + offset,
            bytes.size() - offset,
            file_offset + static_cast<off_t>(offset));
        if (result < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (result == 0) return false;
        offset += static_cast<std::size_t>(result);
    }
    return true;
}

[[nodiscard]] bool read_exact(int fd, os::core::MutableByteSpan bytes) noexcept {
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto result = ::read(fd, bytes.data() + offset, bytes.size() - offset);
        if (result < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (result == 0) return false;
        offset += static_cast<std::size_t>(result);
    }
    return true;
}

[[nodiscard]] bool fsync_retry(int fd) noexcept {
    for (;;) {
        if (::fsync(fd) == 0) return true;
        if (errno != EINTR) return false;
    }
}

[[nodiscard]] os::core::Result<std::array<std::byte, key_registry_binding_bytes_v1>>
make_binding(
    KeyOwner owner,
    const KeyDescriptor& descriptor,
    std::uint32_t version) noexcept {
    if (!os::core::valid_principal(owner.principal) || !descriptor.valid() || version == 0U) {
        return key_error(errors::registry_snapshot_inconsistent);
    }

    std::array<std::byte, key_registry_binding_bytes_v1> binding{};
    write_u32(binding.data(), binding_magic_v1);
    write_u16(binding.data() + 4U, binding_version_v1);
    write_u16(
        binding.data() + 6U,
        static_cast<std::uint16_t>(key_registry_binding_bytes_v1));
    write_u64(binding.data() + 8U, descriptor.id.high);
    write_u64(binding.data() + 16U, descriptor.id.low);
    write_u64(binding.data() + 24U, owner.principal.high);
    write_u64(binding.data() + 32U, owner.principal.low);
    write_u64(binding.data() + 40U, owner.user.value());
    write_u32(binding.data() + 48U, static_cast<std::uint32_t>(descriptor.purpose));
    write_u32(binding.data() + 52U, descriptor.rights);
    write_u32(binding.data() + 56U, version);
    return binding;
}

[[nodiscard]] bool add_size(std::size_t& total, std::size_t amount) noexcept {
    if (total > max_key_registry_snapshot_bytes ||
        amount > max_key_registry_snapshot_bytes - total) {
        return false;
    }
    total += amount;
    return true;
}

} // namespace

os::core::Result<PersistentKeyRegistry>
PersistentKeyRegistry::open(
    os::core::NativeHandle state_directory,
    PersistentKeyProvider& provider) noexcept {
    if (!state_directory.valid()) {
        return key_error(errors::invalid_state_directory);
    }
    struct stat metadata {};
    if (::fstat(state_directory.native(), &metadata) != 0 || !S_ISDIR(metadata.st_mode)) {
        return key_error(errors::invalid_state_directory);
    }

    auto loaded = load_snapshot(state_directory.native(), provider);
    if (!loaded) return loaded.error();
    return PersistentKeyRegistry(std::move(state_directory), provider, loaded.value());
}

void PersistentKeyRegistry::cleanup_provider_references(
    PersistentKeyProvider& provider,
    KeyRegistry& registry) noexcept {
    for (auto& record : registry.records_) {
        if (!record.occupied) continue;
        for (auto& version : record.versions) {
            if (!version.occupied || !version.provider_key.valid()) continue;
            os::core::discard_result(provider.destroy(version.provider_key));
            version.provider_key = {};
            version.destroyed = true;
        }
    }
}

os::core::Result<KeyRegistry>
PersistentKeyRegistry::load_snapshot(
    int directory_fd,
    PersistentKeyProvider& provider) noexcept {
    const int raw_fd = ::openat(
        directory_fd,
        registry_file_name,
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (raw_fd < 0) {
        if (errno == ENOENT) return KeyRegistry(provider);
        return key_error(errors::io_failure);
    }
    os::core::NativeHandle file(raw_fd);

    struct stat metadata {};
    if (::fstat(file.native(), &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
        metadata.st_size < 0) {
        return key_error(errors::io_failure);
    }
    const auto file_size = static_cast<std::uint64_t>(metadata.st_size);
    if (file_size > max_key_registry_snapshot_bytes) {
        return key_error(errors::registry_snapshot_too_large);
    }
    if (file_size < key_registry_snapshot_header_size_v1) {
        return key_error(errors::malformed_registry_snapshot);
    }

    std::array<std::byte, key_registry_snapshot_header_size_v1> header{};
    if (!read_exact(file.native(), header)) {
        return key_error(errors::malformed_registry_snapshot);
    }

    const std::uint32_t magic = read_u32(header.data());
    const std::uint16_t version = read_u16(header.data() + 4U);
    const std::uint16_t header_size = read_u16(header.data() + 6U);
    const std::uint32_t total_size = read_u32(header.data() + 8U);
    const std::uint16_t record_count = read_u16(header.data() + 12U);
    const std::uint16_t reserved0 = read_u16(header.data() + 14U);
    const std::uint32_t total_version_count = read_u32(header.data() + 16U);
    const std::uint32_t reserved1 = read_u32(header.data() + 20U);
    const std::uint64_t reserved2 = read_u64(header.data() + 24U);

    if (magic != key_registry_snapshot_magic_v1) {
        return key_error(errors::malformed_registry_snapshot);
    }
    if (version != key_registry_snapshot_version_v1) {
        return key_error(errors::unsupported_registry_snapshot);
    }
    if (header_size != key_registry_snapshot_header_size_v1 ||
        static_cast<std::uint64_t>(total_size) != file_size ||
        reserved0 != 0U || reserved1 != 0U || reserved2 != 0U ||
        static_cast<std::size_t>(record_count) > max_key_records ||
        static_cast<std::size_t>(total_version_count) > max_key_records * max_key_versions) {
        return key_error(errors::malformed_registry_snapshot);
    }

    KeyRegistry registry(provider);
    std::size_t consumed = header.size();
    std::uint32_t restored_versions = 0U;

    for (std::size_t record_index = 0U;
         record_index < static_cast<std::size_t>(record_count);
         ++record_index) {
        std::array<std::byte, record_header_bytes> record_bytes{};
        if (!read_exact(file.native(), record_bytes) || !add_size(consumed, record_bytes.size())) {
            cleanup_provider_references(provider, registry);
            return key_error(errors::malformed_registry_snapshot);
        }

        const KeyId id{
            read_u64(record_bytes.data()),
            read_u64(record_bytes.data() + 8U),
        };
        const KeyOwner owner{
            .principal = os::core::PrincipalId{
                read_u64(record_bytes.data() + 16U),
                read_u64(record_bytes.data() + 24U),
            },
            .user = os::core::UserId{read_u64(record_bytes.data() + 32U)},
        };
        const std::uint32_t current_version = read_u32(record_bytes.data() + 40U);
        const auto purpose = static_cast<KeyPurpose>(read_u32(record_bytes.data() + 44U));
        const RightsMask rights = read_u32(record_bytes.data() + 48U);
        const std::uint16_t version_count = read_u16(record_bytes.data() + 52U);
        const std::uint16_t flags = read_u16(record_bytes.data() + 54U);
        const std::uint32_t reserved = read_u32(record_bytes.data() + 56U);
        const bool destroyed = (flags & record_flag_destroyed) != 0U;

        if (!id.valid() || !os::core::valid_principal(owner.principal) ||
            current_version == 0U || !valid_purpose(purpose) || !valid_rights(rights) ||
            reserved != 0U || (flags & static_cast<std::uint16_t>(~record_known_flags)) != 0U ||
            registry.find(id) != nullptr ||
            (destroyed && version_count != 0U) ||
            (!destroyed && (version_count == 0U ||
                static_cast<std::size_t>(version_count) > max_key_versions)) ||
            (!destroyed && static_cast<std::uint32_t>(version_count) != current_version)) {
            cleanup_provider_references(provider, registry);
            return key_error(errors::registry_snapshot_inconsistent);
        }

        KeyRecord* target = nullptr;
        for (auto& candidate : registry.records_) {
            if (!candidate.occupied) {
                target = &candidate;
                break;
            }
        }
        if (target == nullptr) {
            cleanup_provider_references(provider, registry);
            return key_error(errors::registry_snapshot_inconsistent);
        }

        target->occupied = true;
        target->destroyed = destroyed;
        target->owner = owner;
        target->descriptor = KeyDescriptor{
            .id = id,
            .version = current_version,
            .purpose = purpose,
            .rights = rights,
        };

        for (std::uint32_t expected_version = 1U;
             expected_version <= static_cast<std::uint32_t>(version_count);
             ++expected_version) {
            std::array<std::byte, version_header_bytes> version_bytes{};
            if (!read_exact(file.native(), version_bytes) ||
                !add_size(consumed, version_bytes.size())) {
                cleanup_provider_references(provider, registry);
                return key_error(errors::malformed_registry_snapshot);
            }
            const std::uint32_t stored_version = read_u32(version_bytes.data());
            const std::uint16_t blob_size = read_u16(version_bytes.data() + 4U);
            const std::uint16_t version_reserved = read_u16(version_bytes.data() + 6U);
            if (stored_version != expected_version || blob_size == 0U ||
                static_cast<std::size_t>(blob_size) > max_persistent_provider_blob_bytes ||
                version_reserved != 0U) {
                cleanup_provider_references(provider, registry);
                return key_error(errors::registry_snapshot_inconsistent);
            }

            std::array<std::byte, max_persistent_provider_blob_bytes> blob{};
            const auto blob_count = static_cast<std::size_t>(blob_size);
            if (!read_exact(file.native(), os::core::MutableByteSpan(blob.data(), blob_count)) ||
                !add_size(consumed, blob_count)) {
                cleanup_provider_references(provider, registry);
                return key_error(errors::malformed_registry_snapshot);
            }

            auto binding = make_binding(owner, target->descriptor, stored_version);
            if (!binding) {
                cleanup_provider_references(provider, registry);
                return binding.error();
            }
            auto restored = provider.restore_reference(
                purpose,
                binding.value(),
                os::core::ByteSpan(blob.data(), blob_count));
            std::fill(blob.begin(), blob.end(), std::byte{0});
            if (!restored || !restored.value().valid()) {
                cleanup_provider_references(provider, registry);
                return restored ? key_error(errors::provider_failure) : restored.error();
            }

            target->versions[static_cast<std::size_t>(expected_version - 1U)] = KeyVersionRecord{
                .occupied = true,
                .destroyed = false,
                .version = stored_version,
                .provider_key = restored.value(),
            };
            ++restored_versions;
        }
    }

    if (consumed != static_cast<std::size_t>(total_size) ||
        restored_versions != total_version_count ||
        registry.record_count() != static_cast<std::size_t>(record_count)) {
        cleanup_provider_references(provider, registry);
        return key_error(errors::registry_snapshot_inconsistent);
    }

    return registry;
}

os::core::Result<void>
PersistentKeyRegistry::persist_candidate(const KeyRegistry& candidate, bool& replaced) noexcept {
    replaced = false;
    if (!state_directory_.valid() || provider_ == nullptr) {
        return key_error(errors::invalid_state_directory);
    }

    if (::unlinkat(state_directory_.native(), registry_temp_file_name, 0) != 0 && errno != ENOENT) {
        return key_error(errors::io_failure);
    }

    const int raw_fd = ::openat(
        state_directory_.native(),
        registry_temp_file_name,
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
        0600);
    if (raw_fd < 0) return key_error(errors::io_failure);
    os::core::NativeHandle file(raw_fd);

    const auto cleanup_temp = [&]() noexcept {
        (void)::unlinkat(state_directory_.native(), registry_temp_file_name, 0);
    };

    std::array<std::byte, key_registry_snapshot_header_size_v1> header{};
    if (!write_all(file.native(), header)) {
        cleanup_temp();
        return key_error(errors::io_failure);
    }

    std::size_t total_size = header.size();
    std::uint16_t record_count = 0U;
    std::uint32_t total_version_count = 0U;

    for (const auto& record : candidate.records_) {
        if (!record.occupied) continue;
        if (!record.descriptor.valid() || !os::core::valid_principal(record.owner.principal)) {
            cleanup_temp();
            return key_error(errors::registry_snapshot_inconsistent);
        }

        const bool destroyed = record.destroyed;
        std::uint16_t version_count = 0U;
        if (!destroyed) {
            if (record.descriptor.version > max_key_versions) {
                cleanup_temp();
                return key_error(errors::registry_snapshot_inconsistent);
            }
            version_count = static_cast<std::uint16_t>(record.descriptor.version);
        }

        std::array<std::byte, record_header_bytes> record_bytes{};
        write_u64(record_bytes.data(), record.descriptor.id.high);
        write_u64(record_bytes.data() + 8U, record.descriptor.id.low);
        write_u64(record_bytes.data() + 16U, record.owner.principal.high);
        write_u64(record_bytes.data() + 24U, record.owner.principal.low);
        write_u64(record_bytes.data() + 32U, record.owner.user.value());
        write_u32(record_bytes.data() + 40U, record.descriptor.version);
        write_u32(record_bytes.data() + 44U, static_cast<std::uint32_t>(record.descriptor.purpose));
        write_u32(record_bytes.data() + 48U, record.descriptor.rights);
        write_u16(record_bytes.data() + 52U, version_count);
        write_u16(record_bytes.data() + 54U, destroyed ? record_flag_destroyed : 0U);
        write_u32(record_bytes.data() + 56U, 0U);

        if (!add_size(total_size, record_bytes.size())) {
            cleanup_temp();
            return key_error(errors::registry_snapshot_too_large);
        }
        if (!write_all(file.native(), record_bytes)) {
            cleanup_temp();
            return key_error(errors::io_failure);
        }
        ++record_count;

        for (std::uint32_t version_number = 1U;
             version_number <= static_cast<std::uint32_t>(version_count);
             ++version_number) {
            const auto* version = candidate.find_version(record, version_number);
            if (version == nullptr || version->destroyed || !version->provider_key.valid()) {
                cleanup_temp();
                return key_error(errors::registry_snapshot_inconsistent);
            }

            auto binding = make_binding(record.owner, record.descriptor, version_number);
            if (!binding) {
                cleanup_temp();
                return binding.error();
            }

            std::array<std::byte, max_persistent_provider_blob_bytes> blob{};
            auto persisted = provider_->persist_reference(
                version->provider_key,
                record.descriptor.purpose,
                binding.value(),
                blob);
            if (!persisted || persisted.value() == 0U || persisted.value() > blob.size() ||
                persisted.value() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) {
                std::fill(blob.begin(), blob.end(), std::byte{0});
                cleanup_temp();
                return persisted ? key_error(errors::provider_failure) : persisted.error();
            }

            std::array<std::byte, version_header_bytes> version_bytes{};
            write_u32(version_bytes.data(), version_number);
            write_u16(version_bytes.data() + 4U, static_cast<std::uint16_t>(persisted.value()));
            write_u16(version_bytes.data() + 6U, 0U);

            if (!add_size(total_size, version_bytes.size()) ||
                !add_size(total_size, persisted.value())) {
                std::fill(blob.begin(), blob.end(), std::byte{0});
                cleanup_temp();
                return key_error(errors::registry_snapshot_too_large);
            }
            if (!write_all(file.native(), version_bytes) ||
                !write_all(file.native(), os::core::ByteSpan(blob.data(), persisted.value()))) {
                std::fill(blob.begin(), blob.end(), std::byte{0});
                cleanup_temp();
                return key_error(errors::io_failure);
            }
            std::fill(blob.begin(), blob.end(), std::byte{0});
            ++total_version_count;
        }
    }

    if (total_size > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        cleanup_temp();
        return key_error(errors::registry_snapshot_too_large);
    }

    write_u32(header.data(), key_registry_snapshot_magic_v1);
    write_u16(header.data() + 4U, key_registry_snapshot_version_v1);
    write_u16(header.data() + 6U, key_registry_snapshot_header_size_v1);
    write_u32(header.data() + 8U, static_cast<std::uint32_t>(total_size));
    write_u16(header.data() + 12U, record_count);
    write_u16(header.data() + 14U, 0U);
    write_u32(header.data() + 16U, total_version_count);
    write_u32(header.data() + 20U, 0U);
    write_u64(header.data() + 24U, 0U);

    if (!pwrite_all(file.native(), header, 0) || !fsync_retry(file.native())) {
        cleanup_temp();
        return key_error(errors::io_failure);
    }
    file.reset();

    if (::renameat(
            state_directory_.native(),
            registry_temp_file_name,
            state_directory_.native(),
            registry_file_name) != 0) {
        cleanup_temp();
        return key_error(errors::io_failure);
    }
    replaced = true;
    if (!fsync_retry(state_directory_.native())) {
        return key_error(errors::io_failure);
    }
    return {};
}

os::core::Result<KeyDescriptor>
PersistentKeyRegistry::create(
    KeyOwner owner,
    KeyId id,
    KeyPurpose purpose,
    RightsMask rights) noexcept {
    KeyRegistry candidate = registry_;
    auto created = candidate.create(owner, id, purpose, rights);
    if (!created) return created.error();

    bool replaced = false;
    auto persisted = persist_candidate(candidate, replaced);
    if (!persisted) {
        if (replaced) {
            registry_ = candidate;
        } else {
            auto* record = candidate.find(id);
            if (record != nullptr) {
                auto* version = candidate.find_version(*record, created.value().version);
                if (version != nullptr && version->provider_key.valid()) {
                    os::core::discard_result(provider_->destroy(version->provider_key));
                }
            }
        }
        return persisted.error();
    }
    registry_ = candidate;
    return created.value();
}

os::core::Result<KeyDescriptor>
PersistentKeyRegistry::rotate(KeyOwner caller, KeyId id) noexcept {
    KeyRegistry candidate = registry_;
    auto rotated = candidate.rotate(caller, id);
    if (!rotated) return rotated.error();

    bool replaced = false;
    auto persisted = persist_candidate(candidate, replaced);
    if (!persisted) {
        if (replaced) {
            registry_ = candidate;
        } else {
            auto* record = candidate.find(id);
            if (record != nullptr) {
                auto* version = candidate.find_version(*record, rotated.value().version);
                if (version != nullptr && version->provider_key.valid()) {
                    os::core::discard_result(provider_->destroy(version->provider_key));
                }
            }
        }
        return persisted.error();
    }
    registry_ = candidate;
    return rotated.value();
}

os::core::Result<void>
PersistentKeyRegistry::destroy(KeyOwner caller, KeyId id) noexcept {
    auto authorized = registry_.authorize_record(caller, id, key_rights::destroy);
    if (!authorized) return authorized.error();

    KeyRegistry candidate = registry_;
    auto* record = candidate.find(id);
    if (record == nullptr) return key_error(errors::not_found);

    std::array<ProviderKeyReference, max_key_versions> references{};
    std::size_t reference_count = 0U;
    for (auto& version : record->versions) {
        if (!version.occupied || version.destroyed) continue;
        if (!version.provider_key.valid()) return key_error(errors::provider_failure);
        references[reference_count++] = version.provider_key;
        version.destroyed = true;
        version.provider_key = {};
    }
    record->destroyed = true;

    bool replaced = false;
    auto persisted = persist_candidate(candidate, replaced);
    if (!persisted && !replaced) return persisted.error();

    registry_ = candidate;
    for (std::size_t index = 0U; index < reference_count; ++index) {
        // Durable logical revocation already replaced the registry snapshot.
        // Provider deletion is best-effort so a late physical cleanup failure
        // cannot make the logical key live again.
        os::core::discard_result(provider_->destroy(references[index]));
    }
    if (!persisted) return persisted.error();
    return {};
}

} // namespace os::keys
