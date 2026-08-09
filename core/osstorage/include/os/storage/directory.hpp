#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/storage/file.hpp>
#include <os/storage/path.hpp>

namespace os::storage {

enum class FileAccess : std::uint8_t {
    read_only = 0,
    write_only = 1,
    read_write = 2,
};

enum class CreateDisposition : std::uint8_t {
    open_existing = 0,
    create_exclusive = 1,
    open_or_create = 2,
};

struct OpenOptions final {
    FileAccess access {FileAccess::read_only};
    CreateDisposition create {CreateDisposition::open_existing};
    bool truncate {false};
};

inline constexpr std::size_t max_atomic_replace_bytes = 1024U * 1024U;

// A Directory is an already-authorized rooted directory object. All child
// resolution is relative to this handle and rejects symlink traversal.
class Directory final {
public:
    Directory() noexcept = default;
    Directory(const Directory&) = delete;
    Directory& operator=(const Directory&) = delete;
    Directory(Directory&&) noexcept = default;
    Directory& operator=(Directory&&) noexcept = default;
    ~Directory() = default;

    [[nodiscard]] bool valid() const noexcept { return handle_.valid(); }

    [[nodiscard]] os::core::Result<File>
    open_file(const RelativePath& path, OpenOptions options = {}) const noexcept;

    [[nodiscard]] os::core::Result<Directory>
    open_directory(const RelativePath& path) const noexcept;

    [[nodiscard]] os::core::Result<void>
    create_directory(const RelativePath& path) const noexcept;

    [[nodiscard]] os::core::Result<void>
    remove_file(const RelativePath& path) const noexcept;

    [[nodiscard]] os::core::Result<void>
    remove_empty_directory(const RelativePath& path) const noexcept;

    // Same-directory temp + fsync + rename + parent fsync. The payload is
    // caller-owned and bounded; this routine performs no payload allocation.
    [[nodiscard]] os::core::Result<void>
    atomic_replace(const RelativePath& path, os::core::ByteSpan contents) const noexcept;

private:
    friend class PrivateRoot;
    explicit Directory(os::core::NativeHandle handle) noexcept
        : handle_(static_cast<os::core::NativeHandle&&>(handle)) {}

    // Only PrivateRoot exposes deliberate root-capability duplication. General
    // application Directory objects remain move-only and are narrowed through
    // open_directory() instead of acquiring an accidental copy operation.
    [[nodiscard]] os::core::Result<Directory> duplicate_root_authority() const noexcept;

    os::core::NativeHandle handle_ {};
};

} // namespace os::storage
