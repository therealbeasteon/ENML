#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/ipc/channel.hpp>
#include <os/ipc/rpc.hpp>
#include <os/keys/ciphertext.hpp>
#include <os/keys/id_source.hpp>
#include <os/keys/key.hpp>
#include <os/keys/registry.hpp>

namespace os::keys {

inline constexpr os::core::ServiceId key_service_id{0x0000F030U};
inline constexpr os::core::ServiceId key_object_service_id{0x0000F031U};
inline constexpr std::uint32_t key_create_operation = 1U;
inline constexpr std::uint32_t key_open_operation = 2U;
inline constexpr std::uint32_t key_object_destroy_operation = 1U;
inline constexpr std::uint32_t key_object_encrypt_operation = 2U;
inline constexpr std::uint32_t key_object_decrypt_operation = 3U;
inline constexpr std::uint32_t key_object_rotate_operation = 4U;
inline constexpr std::size_t max_key_objects = 64U;

class KeyObjectHandle final {
public:
    KeyObjectHandle() noexcept = default;
    KeyObjectHandle(const KeyObjectHandle&) = delete;
    KeyObjectHandle& operator=(const KeyObjectHandle&) = delete;
    KeyObjectHandle(KeyObjectHandle&&) noexcept = default;
    KeyObjectHandle& operator=(KeyObjectHandle&&) noexcept = default;
    ~KeyObjectHandle() = default;

    [[nodiscard]] static os::core::Result<KeyObjectHandle>
    adopt(os::ipc::Channel channel, KeyDescriptor descriptor) noexcept;

    [[nodiscard]] bool valid() const noexcept { return channel_.valid() && descriptor_.valid(); }
    [[nodiscard]] const KeyDescriptor& descriptor() const noexcept { return descriptor_; }

    [[nodiscard]] os::core::Result<std::size_t>
    encrypt(
        os::core::ByteSpan plaintext,
        os::core::ByteSpan aad,
        os::core::MutableByteSpan envelope_output,
        os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<std::size_t>
    decrypt(
        os::core::ByteSpan envelope,
        os::core::ByteSpan aad,
        os::core::MutableByteSpan plaintext_output,
        os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<KeyDescriptor>
    rotate(os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<void>
    destroy(os::core::MutableByteSpan scratch) noexcept;

private:
    KeyObjectHandle(os::ipc::Channel channel, KeyDescriptor descriptor) noexcept
        : channel_(static_cast<os::ipc::Channel&&>(channel)), descriptor_(descriptor) {}

    os::ipc::Channel channel_ {};
    KeyDescriptor descriptor_ {};
    std::uint64_t next_request_id_ {1U};
};

class KeyClient final {
public:
    explicit KeyClient(os::ipc::ClientConnection& connection) noexcept
        : connection_(&connection) {}

    [[nodiscard]] os::core::Result<KeyObjectHandle>
    create_application_data_key(os::core::MutableByteSpan scratch) noexcept;

    [[nodiscard]] os::core::Result<KeyObjectHandle>
    open(KeyId id, os::core::MutableByteSpan scratch) noexcept;

private:
    os::ipc::ClientConnection* connection_ {nullptr};
};

// Bounded single-threaded service core. KeyId and rights metadata can cross
// IPC; provider secret material cannot. All owner checks derive from trusted
// RequestContext.peer and never from caller-supplied owner fields.
class KeyService final {
public:
    KeyService(
        os::ipc::Channel& endpoint,
        os::ipc::PeerIdentityResolver& identity_resolver,
        KeyRegistry& registry,
        KeyIdSource& id_source) noexcept
        : endpoint_(&endpoint),
          identity_resolver_(&identity_resolver),
          registry_(&registry),
          id_source_(&id_source) {}

    [[nodiscard]] os::core::Result<void>
    dispatch_once(os::core::MutableByteSpan receive_buffer, int timeout_ms) noexcept;

    [[nodiscard]] std::size_t live_object_count() const noexcept;

private:
    struct ObjectSlot final {
        bool occupied {false};
        os::core::PeerIdentity peer {};
        KeyOwner owner {};
        KeyDescriptor descriptor {};
        os::ipc::Channel endpoint {};
    };

    os::ipc::Channel* endpoint_ {nullptr};
    os::ipc::PeerIdentityResolver* identity_resolver_ {nullptr};
    KeyRegistry* registry_ {nullptr};
    KeyIdSource* id_source_ {nullptr};
    std::array<ObjectSlot, max_key_objects> objects_ {};
    std::array<std::byte, max_ciphertext_envelope_bytes> operation_buffer_ {};

    [[nodiscard]] os::core::Result<void>
    dispatch_main(os::core::MutableByteSpan receive_buffer) noexcept;

    [[nodiscard]] os::core::Result<void>
    dispatch_object(std::size_t index, os::core::MutableByteSpan receive_buffer) noexcept;

    [[nodiscard]] os::core::Result<std::size_t> allocate_slot() noexcept;
    void clear_slot(std::size_t index) noexcept;
    void clear_slots_for(KeyId id) noexcept;
    void update_slots_for(KeyDescriptor descriptor) noexcept;
};

} // namespace os::keys
