#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/identity.hpp>
#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/rpc.hpp>
#include <os/service/identity.hpp>
#include <os/storage/directory.hpp>
#include <os/storage/file.hpp>
#include <os/storage/private_root.hpp>

namespace os::storage {

// F010 is reserved by the private application bootstrap protocol. Storage uses
// its own stable service-id range so bootstrap and public service traffic can
// never be confused even if they share the OSIP transport format.
inline constexpr os::core::ServiceId storage_service_id{0x0000F020U};
inline constexpr os::core::ServiceId storage_object_service_id{0x0000F021U};
inline constexpr std::uint32_t storage_open_private_root_operation = 1U;

// Private supervisor/control-channel extensions. They deliberately use the
// bootstrap control service rather than the public Storage endpoint. Only
// trusted system code holding a duplicate of the supervisor control channel can
// publish or revoke roots; applications can never invoke these operations.
inline constexpr std::uint32_t storage_control_register_root_operation = 100U;
inline constexpr std::uint32_t storage_control_unregister_root_operation = 101U;

inline constexpr std::size_t max_private_roots = 64U;
inline constexpr std::size_t max_storage_objects = 64U;
// One profile must not be able to consume the global object table. The quota is
// keyed by the durable private-data identity (PrincipalId + UserId), not by a
// transient ProcessId.
inline constexpr std::size_t max_storage_objects_per_principal = 16U;
inline constexpr std::size_t max_storage_io_bytes = 60U * 1024U;
inline constexpr std::size_t max_storage_atomic_bytes = 56U * 1024U;

using RightsMask = std::uint32_t;

enum class StorageObjectType : std::uint32_t {
    file = 1U,
    directory = 2U,
};

namespace file_rights {
inline constexpr RightsMask read = 1U << 0U;
inline constexpr RightsMask write = 1U << 1U;
inline constexpr RightsMask stat = 1U << 2U;
inline constexpr RightsMask sync = 1U << 3U;
inline constexpr RightsMask all = read | write | stat | sync;
} // namespace file_rights

namespace directory_rights {
inline constexpr RightsMask open_file_read = 1U << 0U;
inline constexpr RightsMask open_file_write = 1U << 1U;
inline constexpr RightsMask create_file = 1U << 2U;
inline constexpr RightsMask open_directory = 1U << 3U;
inline constexpr RightsMask create_directory = 1U << 4U;
inline constexpr RightsMask remove_file = 1U << 5U;
inline constexpr RightsMask remove_directory = 1U << 6U;
inline constexpr RightsMask atomic_replace = 1U << 7U;
inline constexpr RightsMask all =
    open_file_read | open_file_write | create_file | open_directory |
    create_directory | remove_file | remove_directory | atomic_replace;
} // namespace directory_rights

class FileObjectHandle final {
public:
    FileObjectHandle() noexcept = default;
    FileObjectHandle(const FileObjectHandle&) = delete;
    FileObjectHandle& operator=(const FileObjectHandle&) = delete;
    FileObjectHandle(FileObjectHandle&&) noexcept = default;
    FileObjectHandle& operator=(FileObjectHandle&&) noexcept = default;
    ~FileObjectHandle() = default;

    [[nodiscard]] static os::core::Result<FileObjectHandle>
    adopt(os::ipc::Channel channel, RightsMask rights) noexcept;

    [[nodiscard]] bool valid() const noexcept { return channel_.valid(); }
    [[nodiscard]] RightsMask rights() const noexcept { return rights_; }

    [[nodiscard]] os::core::Result<std::size_t>
    read_at(
        std::uint64_t offset,
        os::core::MutableByteSpan output,
        os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<std::size_t>
    write_at(
        std::uint64_t offset,
        os::core::ByteSpan input,
        os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<std::uint64_t>
    size(os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<void>
    sync(os::core::MutableByteSpan scratch) noexcept;

private:
    FileObjectHandle(os::ipc::Channel channel, RightsMask rights) noexcept
        : channel_(static_cast<os::ipc::Channel&&>(channel)), rights_(rights) {}

    os::ipc::Channel channel_ {};
    RightsMask rights_ {0U};
    std::uint64_t next_request_id_ {1U};
};

class DirectoryObjectHandle final {
public:
    DirectoryObjectHandle() noexcept = default;
    DirectoryObjectHandle(const DirectoryObjectHandle&) = delete;
    DirectoryObjectHandle& operator=(const DirectoryObjectHandle&) = delete;
    DirectoryObjectHandle(DirectoryObjectHandle&&) noexcept = default;
    DirectoryObjectHandle& operator=(DirectoryObjectHandle&&) noexcept = default;
    ~DirectoryObjectHandle() = default;

    [[nodiscard]] static os::core::Result<DirectoryObjectHandle>
    adopt(os::ipc::Channel channel, RightsMask rights) noexcept;

    [[nodiscard]] bool valid() const noexcept { return channel_.valid(); }
    [[nodiscard]] RightsMask rights() const noexcept { return rights_; }

    [[nodiscard]] os::core::Result<FileObjectHandle>
    open_file(
        const RelativePath& path,
        OpenOptions options,
        os::core::MutableByteSpan scratch) noexcept;

    // The child directory receives exactly requested_rights. The service
    // rejects any bit not already held by this parent capability.
    [[nodiscard]] os::core::Result<DirectoryObjectHandle>
    open_directory(
        const RelativePath& path,
        RightsMask requested_rights,
        os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<void>
    create_directory(
        const RelativePath& path,
        os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<void>
    remove_file(
        const RelativePath& path,
        os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<void>
    remove_empty_directory(
        const RelativePath& path,
        os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<void>
    atomic_replace(
        const RelativePath& path,
        os::core::ByteSpan contents,
        os::core::MutableByteSpan scratch) noexcept;

private:
    DirectoryObjectHandle(os::ipc::Channel channel, RightsMask rights) noexcept
        : channel_(static_cast<os::ipc::Channel&&>(channel)), rights_(rights) {}

    os::ipc::Channel channel_ {};
    RightsMask rights_ {0U};
    std::uint64_t next_request_id_ {1U};
};

class StorageClient final {
public:
    explicit StorageClient(os::ipc::ClientConnection& connection) noexcept
        : connection_(&connection) {}

    [[nodiscard]] os::core::Result<DirectoryObjectHandle>
    open_private_root(os::core::MutableByteSpan scratch) noexcept;

private:
    os::ipc::ClientConnection* connection_ {nullptr};
};

// Trusted-system client for the service's private supervisor control channel.
// The target PrincipalId/UserId is allowed in this payload because possession
// of this channel is itself system authority; the public Storage endpoint never
// accepts caller-supplied identity for root selection.
class StorageControlClient final {
public:
    explicit StorageControlClient(os::ipc::Channel& control) noexcept
        : control_(&control) {}

    [[nodiscard]] os::core::Result<void>
    register_private_root(
        os::core::PrincipalId principal,
        os::core::UserId user,
        const os::core::NativeHandle& directory,
        os::core::MutableByteSpan scratch,
        std::uint32_t timeout_ms) noexcept;

    [[nodiscard]] os::core::Result<void>
    unregister_private_root(
        os::core::PrincipalId principal,
        os::core::UserId user,
        os::core::MutableByteSpan scratch,
        std::uint32_t timeout_ms) noexcept;

private:
    os::ipc::Channel* control_ {nullptr};
    std::uint64_t next_request_id_ {0x8000000000000001ULL};
};

// Trusted policy registry. Applications never register roots and no registration
// method accepts a Linux pathname. The key is durable application principal +
// user; ProcessId is deliberately not part of private-data identity.
class PrivateRootRegistry final {
public:
    [[nodiscard]] os::core::Result<void>
    register_root(
        os::core::PrincipalId principal,
        os::core::UserId user,
        PrivateRoot root) noexcept;

    [[nodiscard]] os::core::Result<void>
    unregister_root(os::core::PrincipalId principal, os::core::UserId user) noexcept;

    [[nodiscard]] PrivateRoot*
    find(os::core::PrincipalId principal, os::core::UserId user) noexcept;

    [[nodiscard]] const PrivateRoot*
    find(os::core::PrincipalId principal, os::core::UserId user) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct Entry final {
        bool occupied {false};
        os::core::PrincipalId principal {};
        os::core::UserId user {};
        PrivateRoot root {};
    };

    std::array<Entry, max_private_roots> entries_ {};
};

// Single-threaded bounded Storage Service core. The main endpoint mints one
// private-root directory capability only after RequestContext identity lookup.
// Object endpoints are possession-based capabilities whose rights are held in
// service memory, not trusted from subsequent request payloads.
class StorageService final {
public:
    StorageService(
        os::ipc::Channel& endpoint,
        os::ipc::PeerIdentityResolver& identity_resolver,
        PrivateRootRegistry& roots) noexcept
        : endpoint_(&endpoint), identity_resolver_(&identity_resolver), roots_(&roots) {}

    [[nodiscard]] os::core::Result<void>
    dispatch_once(os::core::MutableByteSpan receive_buffer, int timeout_ms) noexcept;

    // Storage-specific router for the private supervisor control channel. It
    // preserves the existing identity register/unregister protocol and adds
    // root publication/revocation operations on the same trusted channel.
    [[nodiscard]] os::core::Result<void>
    dispatch_control_once(
        os::ipc::Channel& control,
        os::core::MutableByteSpan receive_buffer,
        os::service::IdentityRegistry& identity_registry) noexcept;

    [[nodiscard]] std::size_t live_object_count() const noexcept;

private:
    enum class SlotKind : std::uint8_t {
        none = 0U,
        root_directory = 1U,
        directory = 2U,
        file = 3U,
    };

    struct ObjectSlot final {
        bool occupied {false};
        SlotKind kind {SlotKind::none};
        RightsMask rights {0U};
        os::core::PrincipalId principal {};
        os::core::UserId user {};
        PrivateRoot* root {nullptr};
        Directory directory {};
        File file {};
        os::ipc::Channel endpoint {};
    };

    os::ipc::Channel* endpoint_ {nullptr};
    os::ipc::PeerIdentityResolver* identity_resolver_ {nullptr};
    PrivateRootRegistry* roots_ {nullptr};
    std::array<ObjectSlot, max_storage_objects> objects_ {};

    [[nodiscard]] os::core::Result<void>
    dispatch_main(os::core::MutableByteSpan receive_buffer) noexcept;

    [[nodiscard]] os::core::Result<void>
    dispatch_object(std::size_t index, os::core::MutableByteSpan receive_buffer) noexcept;

    [[nodiscard]] os::core::Result<std::size_t>
    allocate_slot(os::core::PrincipalId principal, os::core::UserId user) noexcept;
    void clear_slot(std::size_t index) noexcept;
    void clear_objects_for(os::core::PrincipalId principal, os::core::UserId user) noexcept;
};

} // namespace os::storage
