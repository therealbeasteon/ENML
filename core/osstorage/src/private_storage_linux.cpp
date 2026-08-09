#include <os/storage/private_root.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <os/storage/error.hpp>

namespace os::storage {
namespace {

struct ParentAndLeaf final {
    os::core::NativeHandle parent {};
    std::array<char, max_path_segment_bytes + 1U> leaf {};
};

std::atomic<std::uint64_t> atomic_temp_counter {1U};

[[nodiscard]] os::core::Error map_errno(int error) noexcept {
    switch (error) {
    case ENOENT:
        return storage_error(errors::not_found);
    case EEXIST:
        return storage_error(errors::already_exists);
    case ENOTDIR:
        return storage_error(errors::not_directory);
    case EISDIR:
        return storage_error(errors::is_directory);
    case ELOOP:
        return storage_error(errors::path_escape);
    case EACCES:
    case EPERM:
        return storage_error(errors::access_denied);
    case EROFS:
        return storage_error(errors::read_only_filesystem);
    case ENOSPC:
#ifdef EDQUOT
    case EDQUOT:
#endif
        return storage_error(errors::no_space);
    case EMFILE:
    case ENFILE:
        return storage_error(errors::resource_exhausted);
    case ENAMETOOLONG:
        return storage_error(errors::path_too_long);
    default:
        return storage_error(errors::io_failure);
    }
}

[[nodiscard]] os::core::Result<os::core::NativeHandle>
open_directory_from(int root_fd, std::string_view relative) noexcept {
    int current_fd = -1;
    do {
        current_fd = ::openat(root_fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    } while (current_fd < 0 && errno == EINTR);
    if (current_fd < 0) return map_errno(errno);
    os::core::NativeHandle current{current_fd};

    if (relative.empty()) return current;

    std::size_t start = 0U;
    while (start < relative.size()) {
        const auto separator = relative.find('/', start);
        const std::size_t end = separator == std::string_view::npos ? relative.size() : separator;
        const std::size_t length = end - start;

        std::array<char, max_path_segment_bytes + 1U> segment {};
        std::copy_n(relative.data() + static_cast<std::ptrdiff_t>(start), length, segment.data());

        int next_fd = -1;
        do {
            next_fd = ::openat(
                current.native(),
                segment.data(),
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        } while (next_fd < 0 && errno == EINTR);
        if (next_fd < 0) return map_errno(errno);

        current = os::core::NativeHandle{next_fd};
        if (separator == std::string_view::npos) break;
        start = end + 1U;
    }
    return current;
}

[[nodiscard]] os::core::Result<ParentAndLeaf>
resolve_parent(int root_fd, const RelativePath& path) noexcept {
    if (root_fd < 0 || !path.valid()) return storage_error(errors::invalid_path);

    const auto text = path.view();
    const auto separator = text.find_last_of('/');
    const auto parent_text = separator == std::string_view::npos
        ? std::string_view{}
        : text.substr(0U, separator);
    const auto leaf_text = separator == std::string_view::npos
        ? text
        : text.substr(separator + 1U);

    auto parent = open_directory_from(root_fd, parent_text);
    if (!parent) return parent.error();

    ParentAndLeaf result;
    result.parent = std::move(parent).value();
    std::copy(leaf_text.begin(), leaf_text.end(), result.leaf.begin());
    return result;
}

[[nodiscard]] os::core::Result<int>
open_flags(OpenOptions options, bool& readable, bool& writable) noexcept {
    int flags = O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
    switch (options.access) {
    case FileAccess::read_only:
        flags |= O_RDONLY;
        readable = true;
        writable = false;
        break;
    case FileAccess::write_only:
        flags |= O_WRONLY;
        readable = false;
        writable = true;
        break;
    case FileAccess::read_write:
        flags |= O_RDWR;
        readable = true;
        writable = true;
        break;
    default:
        return storage_error(errors::invalid_options);
    }

    switch (options.create) {
    case CreateDisposition::open_existing:
        break;
    case CreateDisposition::create_exclusive:
        flags |= O_CREAT | O_EXCL;
        break;
    case CreateDisposition::open_or_create:
        flags |= O_CREAT;
        break;
    default:
        return storage_error(errors::invalid_options);
    }

    if (options.truncate) {
        if (!writable) return storage_error(errors::invalid_options);
        flags |= O_TRUNC;
    }
    return flags;
}

[[nodiscard]] bool fsync_retry(int fd) noexcept {
    for (;;) {
        if (::fsync(fd) == 0) return true;
        if (errno != EINTR) return false;
    }
}

[[nodiscard]] bool write_all(int fd, os::core::ByteSpan bytes) noexcept {
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const auto count = remaining > static_cast<std::size_t>(SSIZE_MAX)
            ? static_cast<std::size_t>(SSIZE_MAX)
            : remaining;
        const auto result = ::write(fd, bytes.data() + offset, count);
        if (result < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (result == 0) return false;
        offset += static_cast<std::size_t>(result);
    }
    return true;
}

[[nodiscard]] bool make_temp_name(
    std::uint64_t token,
    std::array<char, 64U>& output) noexcept {
    constexpr std::string_view prefix = ".emnl-atomic-";
    std::copy(prefix.begin(), prefix.end(), output.begin());
    auto* begin = output.data() + static_cast<std::ptrdiff_t>(prefix.size());
    auto* end = output.data() + static_cast<std::ptrdiff_t>(output.size() - 1U);
    const auto converted = std::to_chars(begin, end, token, 16);
    if (converted.ec != std::errc{}) return false;
    *converted.ptr = '\0';
    return true;
}

[[nodiscard]] os::core::Result<void>
validate_existing_target(int parent_fd, const char* leaf) noexcept {
    struct stat metadata {};
    if (::fstatat(parent_fd, leaf, &metadata, AT_SYMLINK_NOFOLLOW) == 0) {
        if (S_ISLNK(metadata.st_mode)) return storage_error(errors::path_escape);
        if (S_ISDIR(metadata.st_mode)) return storage_error(errors::is_directory);
        if (!S_ISREG(metadata.st_mode)) return storage_error(errors::not_regular_file);
        return {};
    }
    if (errno == ENOENT) return {};
    return map_errno(errno);
}

} // namespace

os::core::Result<PrivateRoot>
PrivateRoot::adopt_authorized_directory(os::core::NativeHandle directory) noexcept {
    if (!directory.valid()) return storage_error(errors::invalid_root);

    struct stat metadata {};
    if (::fstat(directory.native(), &metadata) != 0 || !S_ISDIR(metadata.st_mode)) {
        return storage_error(errors::invalid_root);
    }

    int rooted_fd = -1;
    do {
        rooted_fd = ::openat(directory.native(), ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    } while (rooted_fd < 0 && errno == EINTR);
    if (rooted_fd < 0) return map_errno(errno);

    return PrivateRoot{Directory{os::core::NativeHandle{rooted_fd}}};
}

os::core::Result<File>
Directory::open_file(const RelativePath& path, OpenOptions options) const noexcept {
    if (!handle_.valid() || !path.valid()) return storage_error(errors::invalid_path);

    auto resolved = resolve_parent(handle_.native(), path);
    if (!resolved) return resolved.error();
    auto parent_leaf = std::move(resolved).value();

    bool readable = false;
    bool writable = false;
    auto flags = open_flags(options, readable, writable);
    if (!flags) return flags.error();

    int fd = -1;
    do {
        fd = ::openat(
            parent_leaf.parent.native(),
            parent_leaf.leaf.data(),
            flags.value(),
            S_IRUSR | S_IWUSR);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) return map_errno(errno);

    os::core::NativeHandle file{fd};
    struct stat metadata {};
    if (::fstat(file.native(), &metadata) != 0) return map_errno(errno);
    if (S_ISDIR(metadata.st_mode)) return storage_error(errors::is_directory);
    if (!S_ISREG(metadata.st_mode)) return storage_error(errors::not_regular_file);

    return File{std::move(file), readable, writable};
}

os::core::Result<Directory>
Directory::open_directory(const RelativePath& path) const noexcept {
    if (!handle_.valid() || !path.valid()) return storage_error(errors::invalid_path);
    auto opened = open_directory_from(handle_.native(), path.view());
    if (!opened) return opened.error();
    return Directory{std::move(opened).value()};
}

os::core::Result<void>
Directory::create_directory(const RelativePath& path) const noexcept {
    if (!handle_.valid() || !path.valid()) return storage_error(errors::invalid_path);
    auto resolved = resolve_parent(handle_.native(), path);
    if (!resolved) return resolved.error();
    auto parent_leaf = std::move(resolved).value();

    if (::mkdirat(
            parent_leaf.parent.native(),
            parent_leaf.leaf.data(),
            S_IRWXU) != 0) {
        return map_errno(errno);
    }
    return {};
}

os::core::Result<void>
Directory::remove_file(const RelativePath& path) const noexcept {
    if (!handle_.valid() || !path.valid()) return storage_error(errors::invalid_path);
    auto resolved = resolve_parent(handle_.native(), path);
    if (!resolved) return resolved.error();
    auto parent_leaf = std::move(resolved).value();

    struct stat metadata {};
    if (::fstatat(
            parent_leaf.parent.native(),
            parent_leaf.leaf.data(),
            &metadata,
            AT_SYMLINK_NOFOLLOW) != 0) {
        return map_errno(errno);
    }
    if (S_ISLNK(metadata.st_mode)) return storage_error(errors::path_escape);
    if (S_ISDIR(metadata.st_mode)) return storage_error(errors::is_directory);
    if (!S_ISREG(metadata.st_mode)) return storage_error(errors::not_regular_file);

    if (::unlinkat(parent_leaf.parent.native(), parent_leaf.leaf.data(), 0) != 0) {
        return map_errno(errno);
    }
    return {};
}

os::core::Result<void>
Directory::remove_empty_directory(const RelativePath& path) const noexcept {
    if (!handle_.valid() || !path.valid()) return storage_error(errors::invalid_path);
    auto resolved = resolve_parent(handle_.native(), path);
    if (!resolved) return resolved.error();
    auto parent_leaf = std::move(resolved).value();

    struct stat metadata {};
    if (::fstatat(
            parent_leaf.parent.native(),
            parent_leaf.leaf.data(),
            &metadata,
            AT_SYMLINK_NOFOLLOW) != 0) {
        return map_errno(errno);
    }
    if (S_ISLNK(metadata.st_mode)) return storage_error(errors::path_escape);
    if (!S_ISDIR(metadata.st_mode)) return storage_error(errors::not_directory);

    if (::unlinkat(
            parent_leaf.parent.native(),
            parent_leaf.leaf.data(),
            AT_REMOVEDIR) != 0) {
        return map_errno(errno);
    }
    return {};
}

os::core::Result<void>
Directory::atomic_replace(const RelativePath& path, os::core::ByteSpan contents) const noexcept {
    if (!handle_.valid() || !path.valid()) return storage_error(errors::invalid_path);
    if (contents.size() > max_atomic_replace_bytes) return storage_error(errors::too_large);

    auto resolved = resolve_parent(handle_.native(), path);
    if (!resolved) return resolved.error();
    auto parent_leaf = std::move(resolved).value();

    auto existing = validate_existing_target(parent_leaf.parent.native(), parent_leaf.leaf.data());
    if (!existing) return existing.error();

    std::array<char, 64U> temp_name {};
    os::core::NativeHandle temp_file;
    bool created = false;
    for (std::uint32_t attempt = 0U; attempt < 64U; ++attempt) {
        const auto token = atomic_temp_counter.fetch_add(1U, std::memory_order_relaxed);
        if (!make_temp_name(token, temp_name)) return storage_error(errors::io_failure);

        int fd = -1;
        do {
            fd = ::openat(
                parent_leaf.parent.native(),
                temp_name.data(),
                O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                S_IRUSR | S_IWUSR);
        } while (fd < 0 && errno == EINTR);

        if (fd >= 0) {
            temp_file = os::core::NativeHandle{fd};
            created = true;
            break;
        }
        if (errno != EEXIST) return map_errno(errno);
    }
    if (!created) return storage_error(errors::resource_exhausted);

    const auto cleanup = [&]() noexcept {
        temp_file.reset();
        static_cast<void>(::unlinkat(parent_leaf.parent.native(), temp_name.data(), 0));
    };

    if (::fchmod(temp_file.native(), S_IRUSR | S_IWUSR) != 0 ||
        !write_all(temp_file.native(), contents) ||
        !fsync_retry(temp_file.native())) {
        const auto error = errno;
        cleanup();
        return map_errno(error);
    }
    temp_file.reset();

    if (::renameat(
            parent_leaf.parent.native(),
            temp_name.data(),
            parent_leaf.parent.native(),
            parent_leaf.leaf.data()) != 0) {
        const auto error = errno;
        cleanup();
        return map_errno(error);
    }

    if (!fsync_retry(parent_leaf.parent.native())) {
        return map_errno(errno);
    }
    return {};
}

os::core::Result<std::size_t>
File::read_at(std::uint64_t offset, os::core::MutableByteSpan output) noexcept {
    if (!handle_.valid()) return storage_error(errors::io_failure);
    if (!readable_) return storage_error(errors::access_denied);
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        return storage_error(errors::invalid_offset);
    }
    if (output.size() > static_cast<std::size_t>(SSIZE_MAX)) {
        return storage_error(errors::too_large);
    }
    if (output.empty()) return static_cast<std::size_t>(0U);

    ssize_t result = -1;
    do {
        result = ::pread(
            handle_.native(),
            output.data(),
            output.size(),
            static_cast<off_t>(offset));
    } while (result < 0 && errno == EINTR);
    if (result < 0) return map_errno(errno);
    return static_cast<std::size_t>(result);
}

os::core::Result<std::size_t>
File::write_at(std::uint64_t offset, os::core::ByteSpan input) noexcept {
    if (!handle_.valid()) return storage_error(errors::io_failure);
    if (!writable_) return storage_error(errors::access_denied);
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        return storage_error(errors::invalid_offset);
    }
    if (input.size() > static_cast<std::size_t>(SSIZE_MAX)) {
        return storage_error(errors::too_large);
    }
    if (input.empty()) return static_cast<std::size_t>(0U);

    ssize_t result = -1;
    do {
        result = ::pwrite(
            handle_.native(),
            input.data(),
            input.size(),
            static_cast<off_t>(offset));
    } while (result < 0 && errno == EINTR);
    if (result < 0) return map_errno(errno);
    return static_cast<std::size_t>(result);
}

os::core::Result<std::uint64_t> File::size() const noexcept {
    if (!handle_.valid()) return storage_error(errors::io_failure);
    struct stat metadata {};
    if (::fstat(handle_.native(), &metadata) != 0) return map_errno(errno);
    if (metadata.st_size < 0) return storage_error(errors::io_failure);
    return static_cast<std::uint64_t>(metadata.st_size);
}

os::core::Result<void> File::sync() noexcept {
    if (!handle_.valid()) return storage_error(errors::io_failure);
    if (!writable_) return storage_error(errors::access_denied);
    if (!fsync_retry(handle_.native())) return map_errno(errno);
    return {};
}

} // namespace os::storage
