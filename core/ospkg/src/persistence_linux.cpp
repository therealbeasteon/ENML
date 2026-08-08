#include <os/package/persistence.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <os/core/error.hpp>
#include <os/core/span.hpp>

namespace os::package {
namespace {

constexpr const char* registry_file_name = "registry-v1.bin";
constexpr const char* registry_temp_file_name = ".registry-v1.tmp";
constexpr std::uint16_t registry_application_flag_active = 1U;
constexpr std::uint16_t registry_application_known_flags = registry_application_flag_active;

[[nodiscard]] constexpr os::core::Error persistence_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::package, code);
}

class Writer final {
public:
    explicit Writer(os::core::MutableByteSpan output) noexcept : output_(output) {}

    [[nodiscard]] bool write_u16(std::uint16_t value) noexcept {
        if (remaining() < 2U) return false;
        output_[cursor_++] = static_cast<std::byte>(value & 0x00FFU);
        output_[cursor_++] = static_cast<std::byte>((value >> 8U) & 0x00FFU);
        return true;
    }

    [[nodiscard]] bool write_u32(std::uint32_t value) noexcept {
        if (remaining() < 4U) return false;
        for (unsigned shift = 0U; shift < 32U; shift += 8U) {
            output_[cursor_++] = static_cast<std::byte>((value >> shift) & 0x000000FFU);
        }
        return true;
    }

    [[nodiscard]] bool write_u64(std::uint64_t value) noexcept {
        if (remaining() < 8U) return false;
        for (unsigned shift = 0U; shift < 64U; shift += 8U) {
            output_[cursor_++] = static_cast<std::byte>((value >> shift) & 0x00000000000000FFULL);
        }
        return true;
    }

    [[nodiscard]] bool write_bytes(os::core::ByteSpan bytes) noexcept {
        if (remaining() < bytes.size()) return false;
        std::copy(bytes.begin(), bytes.end(), output_.begin() + static_cast<std::ptrdiff_t>(cursor_));
        cursor_ += bytes.size();
        return true;
    }

    [[nodiscard]] bool write_text(std::string_view text) noexcept {
        if (remaining() < text.size()) return false;
        for (const char value : text) {
            output_[cursor_++] = static_cast<std::byte>(static_cast<unsigned char>(value));
        }
        return true;
    }

    [[nodiscard]] bool patch_u32(std::size_t offset, std::uint32_t value) noexcept {
        if (offset > output_.size() || output_.size() - offset < 4U) return false;
        for (unsigned index = 0U; index < 4U; ++index) {
            const auto shift = index * 8U;
            output_[offset + index] = static_cast<std::byte>((value >> shift) & 0x000000FFU);
        }
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return cursor_; }

private:
    [[nodiscard]] std::size_t remaining() const noexcept { return output_.size() - cursor_; }

    os::core::MutableByteSpan output_ {};
    std::size_t cursor_ {0U};
};

class Reader final {
public:
    explicit Reader(os::core::ByteSpan input) noexcept : input_(input) {}

    [[nodiscard]] bool read_u16(std::uint16_t& out) noexcept {
        if (remaining() < 2U) return false;
        const auto low = static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(input_[cursor_]));
        const auto high = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(input_[cursor_ + 1U])) << 8U);
        out = static_cast<std::uint16_t>(low | high);
        cursor_ += 2U;
        return true;
    }

    [[nodiscard]] bool read_u32(std::uint32_t& out) noexcept {
        if (remaining() < 4U) return false;
        out = 0U;
        for (std::size_t index = 0U; index < 4U; ++index) {
            out |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(input_[cursor_ + index]))
                << static_cast<unsigned>(index * 8U);
        }
        cursor_ += 4U;
        return true;
    }

    [[nodiscard]] bool read_u64(std::uint64_t& out) noexcept {
        if (remaining() < 8U) return false;
        out = 0U;
        for (std::size_t index = 0U; index < 8U; ++index) {
            out |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(input_[cursor_ + index]))
                << static_cast<unsigned>(index * 8U);
        }
        cursor_ += 8U;
        return true;
    }

    [[nodiscard]] bool read_bytes(std::size_t size, os::core::ByteSpan& out) noexcept {
        if (remaining() < size) return false;
        out = input_.subspan(cursor_, size);
        cursor_ += size;
        return true;
    }

    [[nodiscard]] bool read_text(std::size_t size, std::string_view& out) noexcept {
        if (remaining() < size) return false;
        const auto* data = reinterpret_cast<const char*>(input_.data() + cursor_);
        out = std::string_view(data, size);
        cursor_ += size;
        return true;
    }

    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }

private:
    [[nodiscard]] std::size_t remaining() const noexcept { return input_.size() - cursor_; }

    os::core::ByteSpan input_ {};
    std::size_t cursor_ {0U};
};

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

[[nodiscard]] bool fsync_retry(int fd) noexcept {
    for (;;) {
        if (::fsync(fd) == 0) return true;
        if (errno != EINTR) return false;
    }
}

[[nodiscard]] os::core::Result<std::size_t>
read_snapshot_file(int directory_fd, std::array<std::byte, max_package_registry_snapshot_bytes>& buffer) noexcept {
    const int raw_fd = ::openat(
        directory_fd,
        registry_file_name,
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (raw_fd < 0) {
        if (errno == ENOENT) return static_cast<std::size_t>(0U);
        return persistence_error(persistence_errors::io_failure);
    }
    os::core::NativeHandle file(raw_fd);

    struct stat metadata {};
    if (::fstat(file.native(), &metadata) != 0 || !S_ISREG(metadata.st_mode)) {
        return persistence_error(persistence_errors::io_failure);
    }

    std::size_t used = 0U;
    while (used < buffer.size()) {
        const auto result = ::read(file.native(), buffer.data() + used, buffer.size() - used);
        if (result < 0) {
            if (errno == EINTR) continue;
            return persistence_error(persistence_errors::io_failure);
        }
        if (result == 0) return used;
        used += static_cast<std::size_t>(result);
    }

    std::byte extra {};
    for (;;) {
        const auto result = ::read(file.native(), &extra, 1U);
        if (result < 0 && errno == EINTR) continue;
        if (result == 0) return used;
        if (result > 0) return persistence_error(persistence_errors::snapshot_too_large);
        return persistence_error(persistence_errors::io_failure);
    }
}

} // namespace

os::core::Result<PersistentPackageRegistry>
PersistentPackageRegistry::open(os::core::NativeHandle state_directory) noexcept {
    if (!state_directory.valid()) {
        return persistence_error(persistence_errors::invalid_state_directory);
    }

    struct stat metadata {};
    if (::fstat(state_directory.native(), &metadata) != 0 || !S_ISDIR(metadata.st_mode)) {
        return persistence_error(persistence_errors::invalid_state_directory);
    }

    auto loaded = load_snapshot(state_directory.native());
    if (!loaded) return loaded.error();
    return PersistentPackageRegistry(std::move(state_directory), loaded.value());
}

os::core::Result<PackageRegistry>
PersistentPackageRegistry::load_snapshot(int directory_fd) noexcept {
    std::array<std::byte, max_package_registry_snapshot_bytes> storage {};
    auto read_result = read_snapshot_file(directory_fd, storage);
    if (!read_result) return read_result.error();
    const auto size = read_result.value();
    if (size == 0U) return PackageRegistry{};
    if (size < package_registry_snapshot_header_size_v1) {
        return persistence_error(persistence_errors::malformed_snapshot);
    }

    const os::core::ByteSpan bytes(storage.data(), size);
    Reader reader(bytes);
    std::uint32_t magic = 0U;
    std::uint16_t version = 0U;
    std::uint16_t header_size = 0U;
    std::uint32_t total_size = 0U;
    std::uint16_t application_count = 0U;
    std::uint16_t reserved0 = 0U;
    std::uint32_t total_generation_count = 0U;
    std::uint32_t reserved1 = 0U;

    if (!reader.read_u32(magic) || !reader.read_u16(version) || !reader.read_u16(header_size) ||
        !reader.read_u32(total_size) || !reader.read_u16(application_count) ||
        !reader.read_u16(reserved0) || !reader.read_u32(total_generation_count) ||
        !reader.read_u32(reserved1)) {
        return persistence_error(persistence_errors::malformed_snapshot);
    }

    if (magic != package_registry_snapshot_magic_v1) {
        return persistence_error(persistence_errors::malformed_snapshot);
    }
    if (version != package_registry_snapshot_version_v1) {
        return persistence_error(persistence_errors::unsupported_snapshot_version);
    }
    if (header_size != package_registry_snapshot_header_size_v1 || total_size != size ||
        reserved0 != 0U || reserved1 != 0U ||
        application_count > m1_registry_application_capacity ||
        total_generation_count >
            m1_registry_application_capacity * m1_registry_generations_per_application) {
        return persistence_error(persistence_errors::malformed_snapshot);
    }

    PackageRegistry registry;
    std::array<PackageId, m1_registry_application_capacity> seen_packages {};
    std::size_t seen_count = 0U;
    std::uint32_t decoded_generation_count = 0U;

    for (std::size_t app_index = 0U; app_index < application_count; ++app_index) {
        std::uint16_t package_id_size = 0U;
        std::uint16_t generation_count = 0U;
        std::uint16_t flags = 0U;
        std::uint16_t reserved = 0U;
        std::uint64_t active_generation = 0U;
        os::core::ByteSpan signer_bytes;

        if (!reader.read_u16(package_id_size) || !reader.read_u16(generation_count) ||
            !reader.read_u16(flags) || !reader.read_u16(reserved) ||
            !reader.read_u64(active_generation) ||
            !reader.read_bytes(signer_lineage_id_bytes, signer_bytes)) {
            return persistence_error(persistence_errors::malformed_snapshot);
        }
        if (package_id_size == 0U || package_id_size > max_package_id_bytes ||
            generation_count == 0U || generation_count > m1_registry_generations_per_application ||
            reserved != 0U ||
            (flags & static_cast<std::uint16_t>(~registry_application_known_flags)) != 0U) {
            return persistence_error(persistence_errors::malformed_snapshot);
        }

        const bool has_active = (flags & registry_application_flag_active) != 0U;
        if ((has_active && active_generation == 0U) || (!has_active && active_generation != 0U)) {
            return persistence_error(persistence_errors::snapshot_inconsistent);
        }

        SignerLineageId signer {};
        std::copy(signer_bytes.begin(), signer_bytes.end(), signer.bytes.begin());
        if (!signer.valid()) {
            return persistence_error(persistence_errors::snapshot_inconsistent);
        }

        std::string_view package_text;
        if (!reader.read_text(package_id_size, package_text)) {
            return persistence_error(persistence_errors::malformed_snapshot);
        }
        auto package_id = PackageId::parse(package_text);
        if (!package_id) return persistence_error(persistence_errors::snapshot_inconsistent);

        for (std::size_t seen = 0U; seen < seen_count; ++seen) {
            if (seen_packages[seen] == package_id.value()) {
                return persistence_error(persistence_errors::snapshot_inconsistent);
            }
        }
        seen_packages[seen_count++] = package_id.value();

        const ApplicationIdentity application {package_id.value(), signer};
        for (std::size_t generation_index = 0U; generation_index < generation_count; ++generation_index) {
            std::uint64_t generation_id = 0U;
            os::core::ByteSpan digest_bytes;
            if (!reader.read_u64(generation_id) ||
                !reader.read_bytes(content_digest_bytes, digest_bytes)) {
                return persistence_error(persistence_errors::malformed_snapshot);
            }

            ContentDigest digest {};
            std::copy(digest_bytes.begin(), digest_bytes.end(), digest.bytes.begin());
            const PackageGenerationRecord record {
                application,
                PackageGenerationId(generation_id),
                digest,
            };
            auto stage = registry.stage_generation(record);
            if (!stage) return persistence_error(persistence_errors::snapshot_inconsistent);
            ++decoded_generation_count;
        }

        if (has_active) {
            auto activation = registry.activate(application, PackageGenerationId(active_generation));
            if (!activation) return persistence_error(persistence_errors::snapshot_inconsistent);
        }
    }

    if (decoded_generation_count != total_generation_count || reader.cursor() != bytes.size() ||
        registry.application_count() != application_count) {
        return persistence_error(persistence_errors::snapshot_inconsistent);
    }

    return registry;
}

os::core::Result<void>
PersistentPackageRegistry::persist_candidate(const PackageRegistry& candidate) noexcept {
    std::array<std::byte, max_package_registry_snapshot_bytes> storage {};
    Writer writer(os::core::MutableByteSpan(storage.data(), storage.size()));

    std::uint32_t total_generation_count = 0U;
    for (const auto& slot : candidate.slots_) {
        if (!slot.occupied) continue;
        total_generation_count += static_cast<std::uint32_t>(slot.generation_count);
    }

    if (!writer.write_u32(package_registry_snapshot_magic_v1) ||
        !writer.write_u16(package_registry_snapshot_version_v1) ||
        !writer.write_u16(package_registry_snapshot_header_size_v1) ||
        !writer.write_u32(0U) ||
        !writer.write_u16(static_cast<std::uint16_t>(candidate.application_count())) ||
        !writer.write_u16(0U) ||
        !writer.write_u32(total_generation_count) ||
        !writer.write_u32(0U)) {
        return persistence_error(persistence_errors::snapshot_too_large);
    }

    for (const auto& slot : candidate.slots_) {
        if (!slot.occupied) continue;
        const auto package_text = slot.application.package_id.view();
        if (!slot.application.valid() || package_text.empty() ||
            package_text.size() > max_package_id_bytes || slot.generation_count == 0U ||
            slot.generation_count > m1_registry_generations_per_application ||
            (slot.has_active && slot.active_generation.value() == 0U)) {
            return persistence_error(persistence_errors::snapshot_inconsistent);
        }

        const std::uint16_t flags = slot.has_active
            ? registry_application_flag_active
            : static_cast<std::uint16_t>(0U);
        if (!writer.write_u16(static_cast<std::uint16_t>(package_text.size())) ||
            !writer.write_u16(static_cast<std::uint16_t>(slot.generation_count)) ||
            !writer.write_u16(flags) || !writer.write_u16(0U) ||
            !writer.write_u64(slot.has_active ? slot.active_generation.value() : 0U) ||
            !writer.write_bytes(os::core::ByteSpan(
                slot.application.signer_lineage.bytes.data(),
                slot.application.signer_lineage.bytes.size())) ||
            !writer.write_text(package_text)) {
            return persistence_error(persistence_errors::snapshot_too_large);
        }

        bool active_found = !slot.has_active;
        for (std::size_t index = 0U; index < slot.generation_count; ++index) {
            const auto& generation = slot.generations[index];
            if (!generation.valid() || generation.application != slot.application) {
                return persistence_error(persistence_errors::snapshot_inconsistent);
            }
            if (generation.generation == slot.active_generation) active_found = true;
            if (!writer.write_u64(generation.generation.value()) ||
                !writer.write_bytes(os::core::ByteSpan(
                    generation.content.bytes.data(), generation.content.bytes.size()))) {
                return persistence_error(persistence_errors::snapshot_too_large);
            }
        }
        if (!active_found) {
            return persistence_error(persistence_errors::snapshot_inconsistent);
        }
    }

    if (writer.size() > static_cast<std::size_t>(UINT32_MAX) ||
        !writer.patch_u32(8U, static_cast<std::uint32_t>(writer.size()))) {
        return persistence_error(persistence_errors::snapshot_too_large);
    }

    if (::unlinkat(state_directory_.native(), registry_temp_file_name, 0) != 0 && errno != ENOENT) {
        return persistence_error(persistence_errors::io_failure);
    }

    const int raw_fd = ::openat(
        state_directory_.native(),
        registry_temp_file_name,
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
        S_IRUSR | S_IWUSR);
    if (raw_fd < 0) return persistence_error(persistence_errors::io_failure);
    os::core::NativeHandle temp_file(raw_fd);

    const auto cleanup_temp = [this]() noexcept {
        static_cast<void>(::unlinkat(state_directory_.native(), registry_temp_file_name, 0));
    };

    if (::fchmod(temp_file.native(), S_IRUSR | S_IWUSR) != 0 ||
        !write_all(temp_file.native(), os::core::ByteSpan(storage.data(), writer.size())) ||
        !fsync_retry(temp_file.native())) {
        cleanup_temp();
        return persistence_error(persistence_errors::io_failure);
    }

    if (::renameat(
            state_directory_.native(), registry_temp_file_name,
            state_directory_.native(), registry_file_name) != 0) {
        cleanup_temp();
        return persistence_error(persistence_errors::io_failure);
    }

    if (!fsync_retry(state_directory_.native())) {
        return persistence_error(persistence_errors::io_failure);
    }
    return {};
}

os::core::Result<void>
PersistentPackageRegistry::stage_generation(const PackageGenerationRecord& record) noexcept {
    PackageRegistry candidate = registry_;
    auto staged = candidate.stage_generation(record);
    if (!staged) return staged.error();

    auto persisted = persist_candidate(candidate);
    if (!persisted) return persisted.error();
    registry_ = candidate;
    return {};
}

os::core::Result<void>
PersistentPackageRegistry::activate(
    const ApplicationIdentity& application,
    PackageGenerationId generation) noexcept {
    PackageRegistry candidate = registry_;
    auto activated = candidate.activate(application, generation);
    if (!activated) return activated.error();

    auto persisted = persist_candidate(candidate);
    if (!persisted) return persisted.error();
    registry_ = candidate;
    return {};
}

} // namespace os::package
