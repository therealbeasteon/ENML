#include <os/app/principal_store.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <os/core/error.hpp>
#include <os/core/span.hpp>

namespace os::app {
namespace {

constexpr const char* principal_file_name = "principals-v1.bin";
constexpr const char* principal_temp_file_name = ".principals-v1.tmp";

[[nodiscard]] constexpr os::core::Error principal_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::security, code);
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
read_snapshot(
    int directory_fd,
    std::array<std::byte, max_application_principal_snapshot_bytes>& buffer) noexcept {
    int raw_fd = -1;
    do {
        raw_fd = ::openat(directory_fd, principal_file_name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    } while (raw_fd < 0 && errno == EINTR);
    if (raw_fd < 0) {
        if (errno == ENOENT) return static_cast<std::size_t>(0U);
        return principal_error(principal_errors::io_failure);
    }
    os::core::NativeHandle file{raw_fd};

    struct stat metadata {};
    if (::fstat(file.native(), &metadata) != 0 || !S_ISREG(metadata.st_mode)) {
        return principal_error(principal_errors::io_failure);
    }

    std::size_t used = 0U;
    while (used < buffer.size()) {
        const auto result = ::read(file.native(), buffer.data() + used, buffer.size() - used);
        if (result < 0) {
            if (errno == EINTR) continue;
            return principal_error(principal_errors::io_failure);
        }
        if (result == 0) return used;
        used += static_cast<std::size_t>(result);
    }

    std::byte extra {};
    for (;;) {
        const auto result = ::read(file.native(), &extra, 1U);
        if (result < 0 && errno == EINTR) continue;
        if (result == 0) return used;
        if (result > 0) return principal_error(principal_errors::snapshot_too_large);
        return principal_error(principal_errors::io_failure);
    }
}

[[nodiscard]] bool same_key(
    const ApplicationPrincipalRecord& record,
    const os::package::ApplicationIdentity& application,
    os::core::UserId user) noexcept {
    return record.application == application && record.user == user;
}

} // namespace

os::core::Result<ApplicationPrincipalStore>
ApplicationPrincipalStore::open(os::core::NativeHandle state_directory) noexcept {
    if (!state_directory.valid()) {
        return principal_error(principal_errors::invalid_state_directory);
    }
    struct stat metadata {};
    if (::fstat(state_directory.native(), &metadata) != 0 || !S_ISDIR(metadata.st_mode)) {
        return principal_error(principal_errors::invalid_state_directory);
    }
    return load(std::move(state_directory));
}

os::core::Result<ApplicationPrincipalStore>
ApplicationPrincipalStore::load(os::core::NativeHandle state_directory) noexcept {
    std::array<std::byte, max_application_principal_snapshot_bytes> storage {};
    auto read = read_snapshot(state_directory.native(), storage);
    if (!read) return read.error();
    const auto size = read.value();
    if (size == 0U) {
        return ApplicationPrincipalStore(
            std::move(state_directory),
            std::array<ApplicationPrincipalRecord, m1_application_principal_capacity>{},
            0U,
            1U);
    }
    if (size < application_principal_snapshot_header_size_v1) {
        return principal_error(principal_errors::malformed_snapshot);
    }

    Reader reader{os::core::ByteSpan(storage.data(), size)};
    std::uint32_t magic = 0U;
    std::uint16_t version = 0U;
    std::uint16_t header_size = 0U;
    std::uint32_t total_size = 0U;
    std::uint16_t record_count_wire = 0U;
    std::uint16_t reserved0 = 0U;
    std::uint64_t next_low = 0U;
    std::uint64_t reserved1 = 0U;

    if (!reader.read_u32(magic) || !reader.read_u16(version) || !reader.read_u16(header_size) ||
        !reader.read_u32(total_size) || !reader.read_u16(record_count_wire) ||
        !reader.read_u16(reserved0) || !reader.read_u64(next_low) || !reader.read_u64(reserved1)) {
        return principal_error(principal_errors::malformed_snapshot);
    }
    if (magic != application_principal_snapshot_magic_v1) {
        return principal_error(principal_errors::malformed_snapshot);
    }
    if (version != application_principal_snapshot_version_v1) {
        return principal_error(principal_errors::unsupported_snapshot_version);
    }
    if (header_size != application_principal_snapshot_header_size_v1 || total_size != size ||
        reserved0 != 0U || reserved1 != 0U || next_low == 0U ||
        record_count_wire > m1_application_principal_capacity) {
        return principal_error(principal_errors::malformed_snapshot);
    }

    std::array<ApplicationPrincipalRecord, m1_application_principal_capacity> records {};
    const auto record_count = static_cast<std::size_t>(record_count_wire);
    std::uint64_t highest_low = 0U;

    for (std::size_t index = 0U; index < record_count; ++index) {
        std::uint16_t package_size = 0U;
        std::uint16_t reserved = 0U;
        std::uint64_t user = 0U;
        std::uint64_t principal_low = 0U;
        os::core::ByteSpan signer_bytes;
        std::string_view package_text;

        if (!reader.read_u16(package_size) || !reader.read_u16(reserved) ||
            !reader.read_u64(user) || !reader.read_u64(principal_low) ||
            !reader.read_bytes(os::package::signer_lineage_id_bytes, signer_bytes)) {
            return principal_error(principal_errors::malformed_snapshot);
        }
        if (package_size == 0U || package_size > os::package::max_package_id_bytes ||
            reserved != 0U || principal_low == 0U ||
            !reader.read_text(package_size, package_text)) {
            return principal_error(principal_errors::malformed_snapshot);
        }

        auto package_id = os::package::PackageId::parse(package_text);
        if (!package_id) return principal_error(principal_errors::snapshot_inconsistent);
        os::package::SignerLineageId signer {};
        std::copy(signer_bytes.begin(), signer_bytes.end(), signer.bytes.begin());
        if (!signer.valid()) return principal_error(principal_errors::snapshot_inconsistent);

        const ApplicationPrincipalRecord record{
            .application = os::package::ApplicationIdentity{package_id.value(), signer},
            .user = os::core::UserId{user},
            .principal = os::core::PrincipalId{application_principal_high_v1, principal_low},
        };
        if (!record.valid()) return principal_error(principal_errors::snapshot_inconsistent);

        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (same_key(records[prior], record.application, record.user) ||
                records[prior].principal == record.principal) {
                return principal_error(principal_errors::snapshot_inconsistent);
            }
        }
        records[index] = record;
        if (principal_low > highest_low) highest_low = principal_low;
    }

    if (reader.cursor() != size || next_low <= highest_low) {
        return principal_error(principal_errors::snapshot_inconsistent);
    }

    return ApplicationPrincipalStore(
        std::move(state_directory), records, record_count, next_low);
}

os::core::Result<ApplicationPrincipalRecord>
ApplicationPrincipalStore::lookup(
    const os::package::ApplicationIdentity& application,
    os::core::UserId user) const noexcept {
    if (!application.valid()) return principal_error(principal_errors::invalid_application);
    for (std::size_t index = 0U; index < record_count_; ++index) {
        if (same_key(records_[index], application, user)) return records_[index];
    }
    return principal_error(os::core::errors::security::unknown_process);
}

os::core::Result<ApplicationPrincipalRecord>
ApplicationPrincipalStore::resolve_or_allocate(
    const os::package::ApplicationIdentity& application,
    os::core::UserId user) noexcept {
    if (!application.valid()) return principal_error(principal_errors::invalid_application);
    for (std::size_t index = 0U; index < record_count_; ++index) {
        if (same_key(records_[index], application, user)) return records_[index];
    }
    if (record_count_ >= records_.size()) return principal_error(principal_errors::store_full);
    if (next_principal_low_ == 0U ||
        next_principal_low_ == std::numeric_limits<std::uint64_t>::max()) {
        return principal_error(principal_errors::id_exhausted);
    }

    auto candidate_records = records_;
    const ApplicationPrincipalRecord record{
        .application = application,
        .user = user,
        .principal = os::core::PrincipalId{application_principal_high_v1, next_principal_low_},
    };
    candidate_records[record_count_] = record;
    const auto candidate_count = record_count_ + 1U;
    const auto candidate_next = next_principal_low_ + 1U;

    auto persist = persist_candidate(candidate_records, candidate_count, candidate_next);
    if (!persist) return persist.error();

    records_ = candidate_records;
    record_count_ = candidate_count;
    next_principal_low_ = candidate_next;
    return record;
}

os::core::Result<void>
ApplicationPrincipalStore::persist_candidate(
    const std::array<ApplicationPrincipalRecord, m1_application_principal_capacity>& records,
    std::size_t record_count,
    std::uint64_t next_principal_low) noexcept {
    if (record_count > records.size() || record_count > std::numeric_limits<std::uint16_t>::max() ||
        next_principal_low == 0U) {
        return principal_error(principal_errors::snapshot_inconsistent);
    }

    std::array<std::byte, max_application_principal_snapshot_bytes> storage {};
    Writer writer{os::core::MutableByteSpan(storage.data(), storage.size())};
    if (!writer.write_u32(application_principal_snapshot_magic_v1) ||
        !writer.write_u16(application_principal_snapshot_version_v1) ||
        !writer.write_u16(application_principal_snapshot_header_size_v1) ||
        !writer.write_u32(0U) ||
        !writer.write_u16(static_cast<std::uint16_t>(record_count)) ||
        !writer.write_u16(0U) || !writer.write_u64(next_principal_low) || !writer.write_u64(0U)) {
        return principal_error(principal_errors::snapshot_too_large);
    }

    std::uint64_t highest_low = 0U;
    for (std::size_t index = 0U; index < record_count; ++index) {
        const auto& record = records[index];
        const auto package_text = record.application.package_id.view();
        if (!record.valid() || package_text.empty() ||
            package_text.size() > os::package::max_package_id_bytes ||
            package_text.size() > std::numeric_limits<std::uint16_t>::max()) {
            return principal_error(principal_errors::snapshot_inconsistent);
        }
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (same_key(records[prior], record.application, record.user) ||
                records[prior].principal == record.principal) {
                return principal_error(principal_errors::snapshot_inconsistent);
            }
        }
        if (!writer.write_u16(static_cast<std::uint16_t>(package_text.size())) ||
            !writer.write_u16(0U) || !writer.write_u64(record.user.value()) ||
            !writer.write_u64(record.principal.low) ||
            !writer.write_bytes(os::core::ByteSpan(
                record.application.signer_lineage.bytes.data(),
                record.application.signer_lineage.bytes.size())) ||
            !writer.write_text(package_text)) {
            return principal_error(principal_errors::snapshot_too_large);
        }
        if (record.principal.low > highest_low) highest_low = record.principal.low;
    }
    if (next_principal_low <= highest_low || writer.size() > std::numeric_limits<std::uint32_t>::max()) {
        return principal_error(principal_errors::snapshot_inconsistent);
    }
    if (!writer.patch_u32(8U, static_cast<std::uint32_t>(writer.size()))) {
        return principal_error(principal_errors::snapshot_too_large);
    }

    if (::unlinkat(state_directory_.native(), principal_temp_file_name, 0) != 0 && errno != ENOENT) {
        return principal_error(principal_errors::io_failure);
    }

    int raw_fd = -1;
    do {
        raw_fd = ::openat(
            state_directory_.native(),
            principal_temp_file_name,
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
            S_IRUSR | S_IWUSR);
    } while (raw_fd < 0 && errno == EINTR);
    if (raw_fd < 0) return principal_error(principal_errors::io_failure);
    os::core::NativeHandle file{raw_fd};

    const auto bytes = os::core::ByteSpan(storage.data(), writer.size());
    if (::fchmod(file.native(), S_IRUSR | S_IWUSR) != 0 ||
        !write_all(file.native(), bytes) || !fsync_retry(file.native())) {
        file.reset();
        static_cast<void>(::unlinkat(state_directory_.native(), principal_temp_file_name, 0));
        return principal_error(principal_errors::io_failure);
    }
    file.reset();

    if (::renameat(
            state_directory_.native(), principal_temp_file_name,
            state_directory_.native(), principal_file_name) != 0) {
        static_cast<void>(::unlinkat(state_directory_.native(), principal_temp_file_name, 0));
        return principal_error(principal_errors::io_failure);
    }
    if (!fsync_retry(state_directory_.native())) {
        return principal_error(principal_errors::io_failure);
    }
    return {};
}

} // namespace os::app
