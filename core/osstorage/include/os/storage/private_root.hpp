#pragma once

#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/storage/directory.hpp>

namespace os::storage {

// PrivateRoot is created only from a trusted per-app/per-user data-directory
// handle. It does not accept a Linux pathname and does not expose its native fd.
class PrivateRoot final {
public:
    PrivateRoot() noexcept = default;
    PrivateRoot(const PrivateRoot&) = delete;
    PrivateRoot& operator=(const PrivateRoot&) = delete;
    PrivateRoot(PrivateRoot&&) noexcept = default;
    PrivateRoot& operator=(PrivateRoot&&) noexcept = default;
    ~PrivateRoot() = default;

    [[nodiscard]] static os::core::Result<PrivateRoot>
    adopt_authorized_directory(os::core::NativeHandle directory) noexcept;

    [[nodiscard]] bool valid() const noexcept { return root_.valid(); }

    // Explicitly duplicates the already-authorized root object. This is used
    // when a service mints a new bearer capability so later trusted policy
    // replacement cannot silently retarget an already-issued object.
    [[nodiscard]] os::core::Result<PrivateRoot> duplicate() const noexcept;

    [[nodiscard]] os::core::Result<File>
    open_file(const RelativePath& path, OpenOptions options = {}) const noexcept {
        return root_.open_file(path, options);
    }

    [[nodiscard]] os::core::Result<Directory>
    open_directory(const RelativePath& path) const noexcept {
        return root_.open_directory(path);
    }

    [[nodiscard]] os::core::Result<void>
    create_directory(const RelativePath& path) const noexcept {
        return root_.create_directory(path);
    }

    [[nodiscard]] os::core::Result<void>
    remove_file(const RelativePath& path) const noexcept {
        return root_.remove_file(path);
    }

    [[nodiscard]] os::core::Result<void>
    remove_empty_directory(const RelativePath& path) const noexcept {
        return root_.remove_empty_directory(path);
    }

    [[nodiscard]] os::core::Result<void>
    atomic_replace(const RelativePath& path, os::core::ByteSpan contents) const noexcept {
        return root_.atomic_replace(path, contents);
    }

private:
    explicit PrivateRoot(Directory root) noexcept
        : root_(static_cast<Directory&&>(root)) {}

    Directory root_ {};
};

} // namespace os::storage
