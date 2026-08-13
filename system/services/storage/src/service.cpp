#include <os/storage/service.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

#include <poll.h>

#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>
#include <os/ipc/wire.hpp>
#include <os/storage/error.hpp>

#include "storage_codec.generated.hpp"
#include "storage_types.generated.hpp"

namespace generated = os::services::storage;

namespace os::storage {
namespace {

inline constexpr std::uint32_t object_open_file_operation = 1U;
inline constexpr std::uint32_t object_open_directory_operation = 2U;
inline constexpr std::uint32_t object_create_directory_operation = 3U;
inline constexpr std::uint32_t object_remove_file_operation = 4U;
inline constexpr std::uint32_t object_remove_directory_operation = 5U;
inline constexpr std::uint32_t object_atomic_replace_operation = 6U;
inline constexpr std::uint32_t object_read_at_operation = 16U;
inline constexpr std::uint32_t object_write_at_operation = 17U;
inline constexpr std::uint32_t object_size_operation = 18U;
inline constexpr std::uint32_t object_sync_operation = 19U;

[[nodiscard]] constexpr os::core::Error ipc_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ipc, code);
}

[[nodiscard]] constexpr bool has_flag(std::uint32_t flags, os::ipc::WireFlag flag) noexcept {
    return (flags & os::ipc::flag_value(flag)) != 0U;
}

[[nodiscard]] constexpr bool has_rights(RightsMask held, RightsMask required) noexcept {
    return (held & required) == required;
}

[[nodiscard]] constexpr bool valid_file_rights(RightsMask rights) noexcept {
    return rights != 0U && (rights & ~file_rights::all) == 0U;
}

[[nodiscard]] constexpr bool valid_directory_rights(RightsMask rights) noexcept {
    return rights != 0U && (rights & ~directory_rights::all) == 0U;
}

[[nodiscard]] os::core::Result<void>
validate_scratch(os::core::MutableByteSpan scratch) noexcept {
    if (scratch.size() < os::ipc::max_wire_packet_size) {
        return ipc_error(os::ipc::errors::buffer_too_small);
    }
    return {};
}

[[nodiscard]] os::core::Result<os::ipc::InboundMessage>
object_call(
    os::ipc::Channel& channel,
    std::uint64_t& next_request_id,
    std::uint32_t operation_id,
    os::core::ByteSpan request_payload,
    os::core::MutableByteSpan scratch) noexcept {
    auto scratch_result = validate_scratch(scratch);
    if (!scratch_result) return scratch_result.error();
    if (!channel.valid()) return ipc_error(os::ipc::errors::invalid_channel);
    if (request_payload.size() > os::ipc::max_inline_payload_size) {
        return ipc_error(os::ipc::errors::oversized_message);
    }
    if (next_request_id == 0U) {
        return ipc_error(os::ipc::errors::request_id_exhausted);
    }

    const auto request_id = os::core::RequestId{next_request_id};
    if (next_request_id == std::numeric_limits<std::uint64_t>::max()) {
        next_request_id = 0U;
    } else {
        ++next_request_id;
    }

    const os::ipc::WireHeaderV1 request_header{
        .flags = os::ipc::flag_value(os::ipc::WireFlag::request),
        .service_id = storage_object_service_id,
        .operation_id = operation_id,
        .request_id = request_id,
        .payload_size = static_cast<std::uint32_t>(request_payload.size()),
        .handle_count = 0U,
        .payload_checksum = 0U,
    };

    auto sent = channel.send(request_header, request_payload);
    if (!sent) return sent.error();

    auto received = channel.receive(scratch);
    if (!received) return received.error();
    auto response = std::move(received).value();
    const auto& header = response.header();

    if (!has_flag(header.flags, os::ipc::WireFlag::response) ||
        has_flag(header.flags, os::ipc::WireFlag::request) ||
        has_flag(header.flags, os::ipc::WireFlag::event) ||
        has_flag(header.flags, os::ipc::WireFlag::oneway) ||
        has_flag(header.flags, os::ipc::WireFlag::cancellable) ||
        header.service_id != storage_object_service_id ||
        header.operation_id != operation_id ||
        header.request_id != request_id) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }

    if (has_flag(header.flags, os::ipc::WireFlag::error)) {
        if (header.handle_count != 0U) {
            return ipc_error(os::ipc::errors::protocol_violation);
        }
        os::core::Error remote_error{};
        auto decoded = os::ipc::decode_rpc_error(response.payload(), remote_error);
        if (!decoded) return decoded.error();
        return remote_error;
    }

    return response;
}

[[nodiscard]] bool valid_object_request(const os::ipc::InboundMessage& message) noexcept {
    const auto& header = message.header();
    return has_flag(header.flags, os::ipc::WireFlag::request) &&
        !has_flag(header.flags, os::ipc::WireFlag::response) &&
        !has_flag(header.flags, os::ipc::WireFlag::event) &&
        !has_flag(header.flags, os::ipc::WireFlag::error) &&
        !has_flag(header.flags, os::ipc::WireFlag::oneway) &&
        !has_flag(header.flags, os::ipc::WireFlag::cancellable) &&
        header.service_id == storage_object_service_id &&
        header.request_id.value() != 0U &&
        header.handle_count == 0U;
}

[[nodiscard]] os::core::Result<RelativePath>
decode_path(os::ipc::Decoder& decoder) noexcept {
    auto text = decoder.read_utf8(max_relative_path_bytes);
    if (!text) return text.error();
    return RelativePath::parse(text.value());
}

[[nodiscard]] os::core::Result<RightsMask>
required_directory_rights(OpenOptions options) noexcept {
    RightsMask required = 0U;
    switch (options.access) {
    case FileAccess::read_only:
        required |= directory_rights::open_file_read;
        break;
    case FileAccess::write_only:
        required |= directory_rights::open_file_write;
        break;
    case FileAccess::read_write:
        required |= directory_rights::open_file_read | directory_rights::open_file_write;
        break;
    default:
        return storage_error(errors::invalid_options);
    }

    switch (options.create) {
    case CreateDisposition::open_existing:
        break;
    case CreateDisposition::create_exclusive:
    case CreateDisposition::open_or_create:
        required |= directory_rights::create_file;
        break;
    default:
        return storage_error(errors::invalid_options);
    }

    if (options.truncate &&
        options.access != FileAccess::write_only &&
        options.access != FileAccess::read_write) {
        return storage_error(errors::invalid_options);
    }
    return required;
}

[[nodiscard]] RightsMask rights_for_file(const File& file) noexcept {
    RightsMask rights = file_rights::stat;
    if (file.readable()) rights |= file_rights::read;
    if (file.writable()) rights |= file_rights::write | file_rights::sync;
    return rights;
}

[[nodiscard]] os::core::Result<void>
encode_object_descriptor(
    os::ipc::Encoder& encoder,
    StorageObjectType type,
    RightsMask rights) noexcept {
    auto result = encoder.write_u32_le(static_cast<std::uint32_t>(type));
    if (!result) return result.error();
    return encoder.write_u32_le(rights);
}

struct ObjectDescriptor final {
    StorageObjectType type {StorageObjectType::file};
    RightsMask rights {0U};
};

[[nodiscard]] os::core::Result<ObjectDescriptor>
decode_object_descriptor(os::core::ByteSpan payload) noexcept {
    os::ipc::Decoder decoder{payload};
    auto type = decoder.read_u32_le();
    if (!type) return type.error();
    auto rights = decoder.read_u32_le();
    if (!rights) return rights.error();
    auto end = decoder.require_end();
    if (!end) return ipc_error(os::ipc::errors::protocol_violation);

    if (type.value() == static_cast<std::uint32_t>(StorageObjectType::file)) {
        if (!valid_file_rights(rights.value())) return storage_error(errors::invalid_rights);
        return ObjectDescriptor{StorageObjectType::file, rights.value()};
    }
    if (type.value() == static_cast<std::uint32_t>(StorageObjectType::directory)) {
        if (!valid_directory_rights(rights.value())) return storage_error(errors::invalid_rights);
        return ObjectDescriptor{StorageObjectType::directory, rights.value()};
    }
    return storage_error(errors::invalid_object_type);
}

[[nodiscard]] os::core::Result<os::ipc::Channel>
take_single_endpoint(os::ipc::InboundMessage& message) noexcept {
    if (message.handle_count() != 1U) {
        return ipc_error(os::ipc::errors::handle_count_mismatch);
    }
    auto native = message.take_handle(0U);
    if (!native) return native.error();
    return os::ipc::Channel::adopt(std::move(native).value());
}

[[nodiscard]] os::core::Result<void>
require_empty_success(const os::ipc::InboundMessage& message) noexcept {
    if (message.handle_count() != 0U || !message.payload().empty()) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return {};
}

} // namespace

os::core::Result<FileObjectHandle>
FileObjectHandle::adopt(os::ipc::Channel channel, RightsMask rights) noexcept {
    if (!channel.valid()) return ipc_error(os::ipc::errors::invalid_channel);
    if (!valid_file_rights(rights)) return storage_error(errors::invalid_rights);
    return FileObjectHandle{std::move(channel), rights};
}

os::core::Result<std::size_t>
FileObjectHandle::read_at(
    std::uint64_t offset,
    os::core::MutableByteSpan output,
    os::core::MutableByteSpan scratch) noexcept {
    if (!has_rights(rights_, file_rights::read)) return storage_error(errors::access_denied);
    if (output.size() > max_storage_io_bytes) return storage_error(errors::too_large);
    auto scratch_result = validate_scratch(scratch);
    if (!scratch_result) return scratch_result.error();

    os::ipc::Encoder encoder{scratch.first(os::ipc::max_inline_payload_size)};
    auto result = encoder.write_u64_le(offset);
    if (!result) return result.error();
    result = encoder.write_u32_le(static_cast<std::uint32_t>(output.size()));
    if (!result) return result.error();

    auto call = object_call(
        channel_, next_request_id_, object_read_at_operation, encoder.written(), scratch);
    if (!call) return call.error();
    auto message = std::move(call).value();
    if (message.handle_count() != 0U) return ipc_error(os::ipc::errors::protocol_violation);

    os::ipc::Decoder decoder{message.payload()};
    auto bytes = decoder.read_bytes(max_storage_io_bytes);
    if (!bytes) return bytes.error();
    auto end = decoder.require_end();
    if (!end) return ipc_error(os::ipc::errors::protocol_violation);
    if (bytes.value().size() > output.size()) return ipc_error(os::ipc::errors::protocol_violation);
    std::copy(bytes.value().begin(), bytes.value().end(), output.begin());
    return bytes.value().size();
}

os::core::Result<std::size_t>
FileObjectHandle::write_at(
    std::uint64_t offset,
    os::core::ByteSpan input,
    os::core::MutableByteSpan scratch) noexcept {
    if (!has_rights(rights_, file_rights::write)) return storage_error(errors::access_denied);
    if (input.size() > max_storage_io_bytes) return storage_error(errors::too_large);
    auto scratch_result = validate_scratch(scratch);
    if (!scratch_result) return scratch_result.error();

    os::ipc::Encoder encoder{scratch.first(os::ipc::max_inline_payload_size)};
    auto result = encoder.write_u64_le(offset);
    if (!result) return result.error();
    result = encoder.write_bytes(input, max_storage_io_bytes);
    if (!result) return result.error();

    auto call = object_call(
        channel_, next_request_id_, object_write_at_operation, encoder.written(), scratch);
    if (!call) return call.error();
    auto message = std::move(call).value();
    if (message.handle_count() != 0U) return ipc_error(os::ipc::errors::protocol_violation);

    os::ipc::Decoder decoder{message.payload()};
    auto count = decoder.read_u32_le();
    if (!count) return count.error();
    auto end = decoder.require_end();
    if (!end) return ipc_error(os::ipc::errors::protocol_violation);
    if (static_cast<std::size_t>(count.value()) > input.size()) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return static_cast<std::size_t>(count.value());
}

os::core::Result<std::uint64_t>
FileObjectHandle::size(os::core::MutableByteSpan scratch) noexcept {
    if (!has_rights(rights_, file_rights::stat)) return storage_error(errors::access_denied);
    auto scratch_result = validate_scratch(scratch);
    if (!scratch_result) return scratch_result.error();

    auto call = object_call(channel_, next_request_id_, object_size_operation, {}, scratch);
    if (!call) return call.error();
    auto message = std::move(call).value();
    if (message.handle_count() != 0U) return ipc_error(os::ipc::errors::protocol_violation);

    os::ipc::Decoder decoder{message.payload()};
    auto value = decoder.read_u64_le();
    if (!value) return value.error();
    auto end = decoder.require_end();
    if (!end) return ipc_error(os::ipc::errors::protocol_violation);
    return value.value();
}

os::core::Result<void>
FileObjectHandle::sync(os::core::MutableByteSpan scratch) noexcept {
    if (!has_rights(rights_, file_rights::sync)) return storage_error(errors::access_denied);
    auto scratch_result = validate_scratch(scratch);
    if (!scratch_result) return scratch_result.error();

    auto call = object_call(channel_, next_request_id_, object_sync_operation, {}, scratch);
    if (!call) return call.error();
    auto message = std::move(call).value();
    return require_empty_success(message);
}

os::core::Result<DirectoryObjectHandle>
DirectoryObjectHandle::adopt(os::ipc::Channel channel, RightsMask rights) noexcept {
    if (!channel.valid()) return ipc_error(os::ipc::errors::invalid_channel);
    if (!valid_directory_rights(rights)) return storage_error(errors::invalid_rights);
    return DirectoryObjectHandle{std::move(channel), rights};
}

os::core::Result<FileObjectHandle>
DirectoryObjectHandle::open_file(
    const RelativePath& path,
    OpenOptions options,
    os::core::MutableByteSpan scratch) noexcept {
    auto required = required_directory_rights(options);
    if (!required) return required.error();
    if (!has_rights(rights_, required.value())) return storage_error(errors::access_denied);
    auto scratch_result = validate_scratch(scratch);
    if (!scratch_result) return scratch_result.error();

    os::ipc::Encoder encoder{scratch.first(os::ipc::max_inline_payload_size)};
    auto result = encoder.write_utf8(path.view(), max_relative_path_bytes);
    if (!result) return result.error();
    result = encoder.write_u8(static_cast<std::uint8_t>(options.access));
    if (!result) return result.error();
    result = encoder.write_u8(static_cast<std::uint8_t>(options.create));
    if (!result) return result.error();
    result = encoder.write_bool(options.truncate);
    if (!result) return result.error();

    auto call = object_call(
        channel_, next_request_id_, object_open_file_operation, encoder.written(), scratch);
    if (!call) return call.error();
    auto message = std::move(call).value();
    auto descriptor = decode_object_descriptor(message.payload());
    if (!descriptor) return descriptor.error();
    if (descriptor.value().type != StorageObjectType::file) {
        return storage_error(errors::invalid_object_type);
    }
    auto endpoint = take_single_endpoint(message);
    if (!endpoint) return endpoint.error();
    return FileObjectHandle::adopt(std::move(endpoint).value(), descriptor.value().rights);
}

os::core::Result<DirectoryObjectHandle>
DirectoryObjectHandle::open_directory(
    const RelativePath& path,
    RightsMask requested_rights,
    os::core::MutableByteSpan scratch) noexcept {
    if (!has_rights(rights_, directory_rights::open_directory)) {
        return storage_error(errors::access_denied);
    }
    if (!valid_directory_rights(requested_rights) || !has_rights(rights_, requested_rights)) {
        return storage_error(errors::invalid_rights);
    }
    auto scratch_result = validate_scratch(scratch);
    if (!scratch_result) return scratch_result.error();

    os::ipc::Encoder encoder{scratch.first(os::ipc::max_inline_payload_size)};
    auto result = encoder.write_utf8(path.view(), max_relative_path_bytes);
    if (!result) return result.error();
    result = encoder.write_u32_le(requested_rights);
    if (!result) return result.error();

    auto call = object_call(
        channel_, next_request_id_, object_open_directory_operation, encoder.written(), scratch);
    if (!call) return call.error();
    auto message = std::move(call).value();
    auto descriptor = decode_object_descriptor(message.payload());
    if (!descriptor) return descriptor.error();
    if (descriptor.value().type != StorageObjectType::directory ||
        descriptor.value().rights != requested_rights) {
        return storage_error(errors::invalid_object_type);
    }
    auto endpoint = take_single_endpoint(message);
    if (!endpoint) return endpoint.error();
    return DirectoryObjectHandle::adopt(std::move(endpoint).value(), descriptor.value().rights);
}

os::core::Result<void>
DirectoryObjectHandle::create_directory(
    const RelativePath& path,
    os::core::MutableByteSpan scratch) noexcept {
    if (!has_rights(rights_, directory_rights::create_directory)) {
        return storage_error(errors::access_denied);
    }
    auto scratch_result = validate_scratch(scratch);
    if (!scratch_result) return scratch_result.error();
    os::ipc::Encoder encoder{scratch.first(os::ipc::max_inline_payload_size)};
    auto result = encoder.write_utf8(path.view(), max_relative_path_bytes);
    if (!result) return result.error();
    auto call = object_call(
        channel_, next_request_id_, object_create_directory_operation, encoder.written(), scratch);
    if (!call) return call.error();
    auto message = std::move(call).value();
    return require_empty_success(message);
}

os::core::Result<void>
DirectoryObjectHandle::remove_file(
    const RelativePath& path,
    os::core::MutableByteSpan scratch) noexcept {
    if (!has_rights(rights_, directory_rights::remove_file)) {
        return storage_error(errors::access_denied);
    }
    auto scratch_result = validate_scratch(scratch);
    if (!scratch_result) return scratch_result.error();
    os::ipc::Encoder encoder{scratch.first(os::ipc::max_inline_payload_size)};
    auto result = encoder.write_utf8(path.view(), max_relative_path_bytes);
    if (!result) return result.error();
    auto call = object_call(
        channel_, next_request_id_, object_remove_file_operation, encoder.written(), scratch);
    if (!call) return call.error();
    auto message = std::move(call).value();
    return require_empty_success(message);
}

os::core::Result<void>
DirectoryObjectHandle::remove_empty_directory(
    const RelativePath& path,
    os::core::MutableByteSpan scratch) noexcept {
    if (!has_rights(rights_, directory_rights::remove_directory)) {
        return storage_error(errors::access_denied);
    }
    auto scratch_result = validate_scratch(scratch);
    if (!scratch_result) return scratch_result.error();
    os::ipc::Encoder encoder{scratch.first(os::ipc::max_inline_payload_size)};
    auto result = encoder.write_utf8(path.view(), max_relative_path_bytes);
    if (!result) return result.error();
    auto call = object_call(
        channel_, next_request_id_, object_remove_directory_operation, encoder.written(), scratch);
    if (!call) return call.error();
    auto message = std::move(call).value();
    return require_empty_success(message);
}

os::core::Result<void>
DirectoryObjectHandle::atomic_replace(
    const RelativePath& path,
    os::core::ByteSpan contents,
    os::core::MutableByteSpan scratch) noexcept {
    if (!has_rights(rights_, directory_rights::atomic_replace)) {
        return storage_error(errors::access_denied);
    }
    if (contents.size() > max_storage_atomic_bytes) return storage_error(errors::too_large);
    auto scratch_result = validate_scratch(scratch);
    if (!scratch_result) return scratch_result.error();
    os::ipc::Encoder encoder{scratch.first(os::ipc::max_inline_payload_size)};
    auto result = encoder.write_utf8(path.view(), max_relative_path_bytes);
    if (!result) return result.error();
    result = encoder.write_bytes(contents, max_storage_atomic_bytes);
    if (!result) return result.error();
    auto call = object_call(
        channel_, next_request_id_, object_atomic_replace_operation, encoder.written(), scratch);
    if (!call) return call.error();
    auto message = std::move(call).value();
    return require_empty_success(message);
}

os::core::Result<DirectoryObjectHandle>
StorageClient::open_private_root(os::core::MutableByteSpan scratch) noexcept {
    if (connection_ == nullptr) return ipc_error(os::ipc::errors::invalid_channel);
    auto scratch_result = validate_scratch(scratch);
    if (!scratch_result) return scratch_result.error();

    generated::OpenPrivateRootRequest request{};
    os::ipc::Encoder encoder{scratch.first(os::ipc::max_inline_payload_size)};
    auto encoded = generated::encode_open_private_root_request(encoder, request);
    if (!encoded) return encoded.error();

    auto call = connection_->call(
        storage_service_id,
        storage_open_private_root_operation,
        encoder.written(),
        scratch);
    if (!call) return call.error();
    auto message = std::move(call).value();

    os::ipc::Decoder decoder{message.payload()};
    auto response = generated::decode_open_private_root_response(decoder);
    if (!response) return response.error();
    if (response.value().object_type != static_cast<std::uint32_t>(StorageObjectType::directory) ||
        !valid_directory_rights(response.value().rights)) {
        return storage_error(errors::invalid_object_type);
    }

    auto endpoint = take_single_endpoint(message);
    if (!endpoint) return endpoint.error();
    return DirectoryObjectHandle::adopt(std::move(endpoint).value(), response.value().rights);
}

os::core::Result<void>
PrivateRootRegistry::register_root(
    os::core::PrincipalId principal,
    os::core::UserId user,
    PrivateRoot root) noexcept {
    if (!os::core::valid_principal(principal) || !root.valid()) {
        return storage_error(errors::invalid_root);
    }
    if (find(principal, user) != nullptr) {
        return storage_error(errors::root_already_registered);
    }
    for (auto& entry : entries_) {
        if (!entry.occupied) {
            entry.occupied = true;
            entry.principal = principal;
            entry.user = user;
            entry.root = std::move(root);
            return {};
        }
    }
    return storage_error(errors::object_limit);
}

PrivateRoot*
PrivateRootRegistry::find(os::core::PrincipalId principal, os::core::UserId user) noexcept {
    for (auto& entry : entries_) {
        if (entry.occupied && entry.principal == principal && entry.user == user) {
            return &entry.root;
        }
    }
    return nullptr;
}

const PrivateRoot*
PrivateRootRegistry::find(os::core::PrincipalId principal, os::core::UserId user) const noexcept {
    for (const auto& entry : entries_) {
        if (entry.occupied && entry.principal == principal && entry.user == user) {
            return &entry.root;
        }
    }
    return nullptr;
}

std::size_t PrivateRootRegistry::size() const noexcept {
    std::size_t count = 0U;
    for (const auto& entry : entries_) {
        if (entry.occupied) ++count;
    }
    return count;
}

os::core::Result<std::size_t> StorageService::allocate_slot(
    os::core::PrincipalId principal,
    os::core::UserId user) noexcept {
    std::size_t owned = 0U;
    for (const auto& slot : objects_) {
        if (slot.occupied && slot.principal == principal && slot.user == user) {
            ++owned;
        }
    }
    if (owned >= max_storage_objects_per_principal) {
        return storage_error(errors::principal_object_limit);
    }

    for (std::size_t index = 0U; index < objects_.size(); ++index) {
        if (!objects_[index].occupied) return index;
    }
    return storage_error(errors::object_limit);
}

void StorageService::clear_slot(std::size_t index) noexcept {
    if (index < objects_.size()) {
        objects_[index] = ObjectSlot{};
    }
}

std::size_t StorageService::live_object_count() const noexcept {
    std::size_t count = 0U;
    for (const auto& slot : objects_) {
        if (slot.occupied) ++count;
    }
    return count;
}

std::size_t
StorageService::collect_wait_descriptors(int* out, std::size_t capacity) const noexcept {
    if (out == nullptr || capacity == 0U || endpoint_ == nullptr || !endpoint_->valid()) {
        return 0U;
    }

    std::size_t count = 0U;
    out[count] = endpoint_->native_fd();
    ++count;

    for (std::size_t index = 0U; index < objects_.size() && count < capacity; ++index) {
        if (!objects_[index].occupied) {
            continue;
        }
        out[count] = objects_[index].endpoint.native_fd();
        ++count;
    }
    return count;
}

os::core::Result<void>
StorageService::dispatch_once(
    os::core::MutableByteSpan receive_buffer,
    int timeout_ms) noexcept {
    if (endpoint_ == nullptr || identity_resolver_ == nullptr || roots_ == nullptr || !endpoint_->valid()) {
        return ipc_error(os::ipc::errors::invalid_channel);
    }
    auto scratch_result = validate_scratch(receive_buffer);
    if (!scratch_result) return scratch_result.error();
    if (timeout_ms < -1) return os::core::core_error(os::core::errors::core::invalid_argument);

    // Only live descriptors are handed to the kernel. Passing the full table
    // capacity is not merely wasteful: poll() fails with EINVAL when nfds
    // exceeds RLIMIT_NOFILE, and the default service sandbox caps open files
    // at 32 while this table is max_storage_objects + 1 = 65. Polling the
    // whole table therefore made the service unable to run under its own
    // default hardening profile.
    std::array<pollfd, max_storage_objects + 1U> descriptors{};
    // Maps each compacted poll slot back to its object slot. Index 0 is the
    // main endpoint and owns no object.
    std::array<std::size_t, max_storage_objects + 1U> slot_objects{};

    std::size_t live = 0U;
    descriptors[live].fd = endpoint_->native_fd();
    descriptors[live].events = POLLIN;
    descriptors[live].revents = 0;
    slot_objects[live] = 0U;
    ++live;

    for (std::size_t index = 0U; index < objects_.size(); ++index) {
        if (!objects_[index].occupied) {
            continue;
        }
        descriptors[live].fd = objects_[index].endpoint.native_fd();
        descriptors[live].events = POLLIN;
        descriptors[live].revents = 0;
        slot_objects[live] = index;
        ++live;
    }

    int poll_result = -1;
    do {
        poll_result = ::poll(
            descriptors.data(),
            static_cast<nfds_t>(live),
            timeout_ms);
    } while (poll_result < 0 && errno == EINTR);
    if (poll_result < 0) return storage_error(errors::io_failure);
    if (poll_result == 0) return {};

    if ((descriptors[0].revents & POLLIN) != 0) {
        return dispatch_main(receive_buffer);
    }
    if ((descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return ipc_error(os::ipc::errors::peer_died);
    }

    for (std::size_t slot = 1U; slot < live; ++slot) {
        const auto revents = descriptors[slot].revents;
        const auto index = slot_objects[slot];
        if ((revents & POLLIN) != 0) {
            return dispatch_object(index, receive_buffer);
        }
        if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            clear_slot(index);
            return {};
        }
    }
    return {};
}

os::core::Result<void>
StorageService::dispatch_main(os::core::MutableByteSpan receive_buffer) noexcept {
    auto received = endpoint_->receive(receive_buffer);
    if (!received) return received.error();
    auto message = std::move(received).value();

    auto context = os::ipc::validate_rpc_request(message, storage_service_id, *identity_resolver_);
    if (!context) {
        return os::ipc::send_rpc_error(*endpoint_, message.header(), context.error());
    }
    if (message.header().operation_id != storage_open_private_root_operation) {
        return os::ipc::send_rpc_error(
            *endpoint_, message.header(),
            os::core::make_error(
                os::core::ErrorDomain::service,
                os::core::errors::service::unknown_operation));
    }

    os::ipc::Decoder decoder{message.payload()};
    auto request = generated::decode_open_private_root_request(decoder);
    if (!request) {
        return os::ipc::send_rpc_error(*endpoint_, message.header(), request.error());
    }

    auto* root = roots_->find(context.value().peer.principal, context.value().peer.user);
    if (root == nullptr) {
        return os::ipc::send_rpc_error(
            *endpoint_, message.header(), storage_error(errors::root_not_registered));
    }

    auto slot_index = allocate_slot(
        context.value().peer.principal,
        context.value().peer.user);
    if (!slot_index) {
        return os::ipc::send_rpc_error(*endpoint_, message.header(), slot_index.error());
    }
    auto pair_result = os::ipc::Channel::create_local_pair();
    if (!pair_result) {
        return os::ipc::send_rpc_error(*endpoint_, message.header(), pair_result.error());
    }
    auto pair = std::move(pair_result).value();

    auto& slot = objects_[slot_index.value()];
    slot.occupied = true;
    slot.kind = SlotKind::root_directory;
    slot.rights = directory_rights::all;
    slot.principal = context.value().peer.principal;
    slot.user = context.value().peer.user;
    slot.root = root;
    slot.endpoint = std::move(pair[0]);

    generated::OpenPrivateRootResponse response{
        .object_type = static_cast<std::uint32_t>(StorageObjectType::directory),
        .rights = directory_rights::all,
    };
    std::array<std::byte, 16U> response_storage{};
    os::ipc::Encoder response_encoder{response_storage};
    auto encoded = generated::encode_open_private_root_response(response_encoder, response);
    if (!encoded) {
        clear_slot(slot_index.value());
        return os::ipc::send_rpc_error(*endpoint_, message.header(), encoded.error());
    }

    auto client_endpoint = pair[1].take_native_handle_for_transfer();
    std::span<const os::core::NativeHandle> handles{&client_endpoint, 1U};
    auto sent = os::ipc::send_rpc_response(
        *endpoint_, message.header(), response_encoder.written(), handles);
    if (!sent) {
        clear_slot(slot_index.value());
        return sent.error();
    }
    return {};
}

os::core::Result<void>
StorageService::dispatch_object(
    std::size_t index,
    os::core::MutableByteSpan receive_buffer) noexcept {
    if (index >= objects_.size() || !objects_[index].occupied) {
        return os::core::core_error(os::core::errors::core::invalid_argument);
    }
    auto& slot = objects_[index];

    auto received = slot.endpoint.receive(receive_buffer);
    if (!received) {
        if (received.error().domain == os::core::ErrorDomain::ipc &&
            received.error().code == os::ipc::errors::peer_died) {
            clear_slot(index);
            return {};
        }
        return received.error();
    }
    auto message = std::move(received).value();
    if (!valid_object_request(message)) {
        return os::ipc::send_rpc_error(
            slot.endpoint,
            message.header(),
            ipc_error(os::ipc::errors::protocol_violation));
    }

    const auto is_directory =
        slot.kind == SlotKind::root_directory || slot.kind == SlotKind::directory;
    const auto is_file = slot.kind == SlotKind::file;

    const auto open_file_from_slot = [&](const RelativePath& path, OpenOptions options)
        -> os::core::Result<File> {
        if (slot.kind == SlotKind::root_directory && slot.root != nullptr) {
            return slot.root->open_file(path, options);
        }
        if (slot.kind == SlotKind::directory) {
            return slot.directory.open_file(path, options);
        }
        return storage_error(errors::invalid_object_type);
    };

    const auto open_directory_from_slot = [&](const RelativePath& path)
        -> os::core::Result<Directory> {
        if (slot.kind == SlotKind::root_directory && slot.root != nullptr) {
            return slot.root->open_directory(path);
        }
        if (slot.kind == SlotKind::directory) {
            return slot.directory.open_directory(path);
        }
        return storage_error(errors::invalid_object_type);
    };

    const auto create_directory_from_slot = [&](const RelativePath& path)
        -> os::core::Result<void> {
        if (slot.kind == SlotKind::root_directory && slot.root != nullptr) {
            return slot.root->create_directory(path);
        }
        if (slot.kind == SlotKind::directory) {
            return slot.directory.create_directory(path);
        }
        return storage_error(errors::invalid_object_type);
    };

    const auto remove_file_from_slot = [&](const RelativePath& path)
        -> os::core::Result<void> {
        if (slot.kind == SlotKind::root_directory && slot.root != nullptr) {
            return slot.root->remove_file(path);
        }
        if (slot.kind == SlotKind::directory) {
            return slot.directory.remove_file(path);
        }
        return storage_error(errors::invalid_object_type);
    };

    const auto remove_directory_from_slot = [&](const RelativePath& path)
        -> os::core::Result<void> {
        if (slot.kind == SlotKind::root_directory && slot.root != nullptr) {
            return slot.root->remove_empty_directory(path);
        }
        if (slot.kind == SlotKind::directory) {
            return slot.directory.remove_empty_directory(path);
        }
        return storage_error(errors::invalid_object_type);
    };

    // The canonical root-relative address of `path` as seen from this slot.
    // Only the private root itself has no namespace address of its own; every
    // descendant carries one. Composing through it is what stops a child
    // directory capability from presenting "file" as though it named a
    // root-level object, which is the reason join_relative_paths exists.
    const auto canonical_namespace_path = [&](const RelativePath& path)
        -> os::core::Result<RelativePath> {
        if (!slot.namespace_path.valid()) return path;
        return join_relative_paths(slot.namespace_path, path);
    };

    const auto atomic_replace_from_slot = [&](const RelativePath& path, os::core::ByteSpan contents)
        -> os::core::Result<void> {
        const bool addressable =
            (slot.kind == SlotKind::root_directory && slot.root != nullptr) ||
            slot.kind == SlotKind::directory;
        if (!addressable) return storage_error(errors::invalid_object_type);

        // With a protected handler installed, durable replacement goes through
        // the encrypted publication engine instead of the substrate. Identity
        // is taken from the slot - server-held and fixed when the capability
        // was minted - never from the request, and the path handed over is the
        // canonical root-relative one rather than the caller's child-relative
        // view.
        if (protected_replace_ != nullptr) {
            auto canonical = canonical_namespace_path(path);
            if (!canonical) return canonical.error();
            return protected_replace_->atomic_replace(
                slot.principal, slot.user, canonical.value(), contents);
        }

        if (slot.kind == SlotKind::root_directory) {
            return slot.root->atomic_replace(path, contents);
        }
        return slot.directory.atomic_replace(path, contents);
    };

    switch (message.header().operation_id) {
    case object_open_file_operation: {
        if (!is_directory) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), storage_error(errors::invalid_object_type));
        }
        os::ipc::Decoder decoder{message.payload()};
        auto path = decode_path(decoder);
        if (!path) return os::ipc::send_rpc_error(slot.endpoint, message.header(), path.error());
        auto access_raw = decoder.read_u8();
        if (!access_raw) return os::ipc::send_rpc_error(slot.endpoint, message.header(), access_raw.error());
        auto create_raw = decoder.read_u8();
        if (!create_raw) return os::ipc::send_rpc_error(slot.endpoint, message.header(), create_raw.error());
        auto truncate = decoder.read_bool();
        if (!truncate) return os::ipc::send_rpc_error(slot.endpoint, message.header(), truncate.error());
        auto end = decoder.require_end();
        if (!end) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), ipc_error(os::ipc::errors::protocol_violation));
        }
        if (access_raw.value() > static_cast<std::uint8_t>(FileAccess::read_write) ||
            create_raw.value() > static_cast<std::uint8_t>(CreateDisposition::open_or_create)) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), storage_error(errors::invalid_options));
        }
        const OpenOptions options{
            .access = static_cast<FileAccess>(access_raw.value()),
            .create = static_cast<CreateDisposition>(create_raw.value()),
            .truncate = truncate.value(),
        };
        auto required = required_directory_rights(options);
        if (!required) {
            return os::ipc::send_rpc_error(slot.endpoint, message.header(), required.error());
        }
        if (!has_rights(slot.rights, required.value())) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), storage_error(errors::access_denied));
        }

        auto opened = open_file_from_slot(path.value(), options);
        if (!opened) return os::ipc::send_rpc_error(slot.endpoint, message.header(), opened.error());
        auto file = std::move(opened).value();
        const auto child_rights = rights_for_file(file);

        auto child_namespace = canonical_namespace_path(path.value());
        if (!child_namespace) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), child_namespace.error());
        }

        auto child_index = allocate_slot(slot.principal, slot.user);
        if (!child_index) {
            return os::ipc::send_rpc_error(slot.endpoint, message.header(), child_index.error());
        }
        auto pair_result = os::ipc::Channel::create_local_pair();
        if (!pair_result) {
            return os::ipc::send_rpc_error(slot.endpoint, message.header(), pair_result.error());
        }
        auto pair = std::move(pair_result).value();
        auto& child = objects_[child_index.value()];
        child.occupied = true;
        child.kind = SlotKind::file;
        child.rights = child_rights;
        child.principal = slot.principal;
        child.user = slot.user;
        child.namespace_path = child_namespace.value();
        child.file = std::move(file);
        child.endpoint = std::move(pair[0]);

        std::array<std::byte, 8U> response_storage{};
        os::ipc::Encoder response_encoder{response_storage};
        auto encoded = encode_object_descriptor(
            response_encoder, StorageObjectType::file, child_rights);
        if (!encoded) {
            clear_slot(child_index.value());
            return os::ipc::send_rpc_error(slot.endpoint, message.header(), encoded.error());
        }
        auto client_endpoint = pair[1].take_native_handle_for_transfer();
        std::span<const os::core::NativeHandle> handles{&client_endpoint, 1U};
        auto sent = os::ipc::send_rpc_response(
            slot.endpoint, message.header(), response_encoder.written(), handles);
        if (!sent) clear_slot(child_index.value());
        return sent;
    }
    case object_open_directory_operation: {
        if (!is_directory || !has_rights(slot.rights, directory_rights::open_directory)) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), storage_error(errors::access_denied));
        }
        os::ipc::Decoder decoder{message.payload()};
        auto path = decode_path(decoder);
        if (!path) return os::ipc::send_rpc_error(slot.endpoint, message.header(), path.error());
        auto requested = decoder.read_u32_le();
        if (!requested) return os::ipc::send_rpc_error(slot.endpoint, message.header(), requested.error());
        auto end = decoder.require_end();
        if (!end) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), ipc_error(os::ipc::errors::protocol_violation));
        }
        if (!valid_directory_rights(requested.value()) ||
            !has_rights(slot.rights, requested.value())) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), storage_error(errors::invalid_rights));
        }

        auto opened = open_directory_from_slot(path.value());
        if (!opened) return os::ipc::send_rpc_error(slot.endpoint, message.header(), opened.error());
        auto directory = std::move(opened).value();

        auto child_namespace = canonical_namespace_path(path.value());
        if (!child_namespace) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), child_namespace.error());
        }

        auto child_index = allocate_slot(slot.principal, slot.user);
        if (!child_index) {
            return os::ipc::send_rpc_error(slot.endpoint, message.header(), child_index.error());
        }
        auto pair_result = os::ipc::Channel::create_local_pair();
        if (!pair_result) {
            return os::ipc::send_rpc_error(slot.endpoint, message.header(), pair_result.error());
        }
        auto pair = std::move(pair_result).value();
        auto& child = objects_[child_index.value()];
        child.occupied = true;
        child.kind = SlotKind::directory;
        child.rights = requested.value();
        child.principal = slot.principal;
        child.user = slot.user;
        child.namespace_path = child_namespace.value();
        child.directory = std::move(directory);
        child.endpoint = std::move(pair[0]);

        std::array<std::byte, 8U> response_storage{};
        os::ipc::Encoder response_encoder{response_storage};
        auto encoded = encode_object_descriptor(
            response_encoder, StorageObjectType::directory, requested.value());
        if (!encoded) {
            clear_slot(child_index.value());
            return os::ipc::send_rpc_error(slot.endpoint, message.header(), encoded.error());
        }
        auto client_endpoint = pair[1].take_native_handle_for_transfer();
        std::span<const os::core::NativeHandle> handles{&client_endpoint, 1U};
        auto sent = os::ipc::send_rpc_response(
            slot.endpoint, message.header(), response_encoder.written(), handles);
        if (!sent) clear_slot(child_index.value());
        return sent;
    }
    case object_create_directory_operation: {
        if (!is_directory || !has_rights(slot.rights, directory_rights::create_directory)) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), storage_error(errors::access_denied));
        }
        os::ipc::Decoder decoder{message.payload()};
        auto path = decode_path(decoder);
        if (!path) return os::ipc::send_rpc_error(slot.endpoint, message.header(), path.error());
        auto end = decoder.require_end();
        if (!end) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), ipc_error(os::ipc::errors::protocol_violation));
        }
        auto result = create_directory_from_slot(path.value());
        if (!result) return os::ipc::send_rpc_error(slot.endpoint, message.header(), result.error());
        return os::ipc::send_rpc_response(slot.endpoint, message.header(), {});
    }
    case object_remove_file_operation: {
        if (!is_directory || !has_rights(slot.rights, directory_rights::remove_file)) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), storage_error(errors::access_denied));
        }
        os::ipc::Decoder decoder{message.payload()};
        auto path = decode_path(decoder);
        if (!path) return os::ipc::send_rpc_error(slot.endpoint, message.header(), path.error());
        auto end = decoder.require_end();
        if (!end) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), ipc_error(os::ipc::errors::protocol_violation));
        }
        auto result = remove_file_from_slot(path.value());
        if (!result) return os::ipc::send_rpc_error(slot.endpoint, message.header(), result.error());
        return os::ipc::send_rpc_response(slot.endpoint, message.header(), {});
    }
    case object_remove_directory_operation: {
        if (!is_directory || !has_rights(slot.rights, directory_rights::remove_directory)) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), storage_error(errors::access_denied));
        }
        os::ipc::Decoder decoder{message.payload()};
        auto path = decode_path(decoder);
        if (!path) return os::ipc::send_rpc_error(slot.endpoint, message.header(), path.error());
        auto end = decoder.require_end();
        if (!end) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), ipc_error(os::ipc::errors::protocol_violation));
        }
        auto result = remove_directory_from_slot(path.value());
        if (!result) return os::ipc::send_rpc_error(slot.endpoint, message.header(), result.error());
        return os::ipc::send_rpc_response(slot.endpoint, message.header(), {});
    }
    case object_atomic_replace_operation: {
        if (!is_directory || !has_rights(slot.rights, directory_rights::atomic_replace)) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), storage_error(errors::access_denied));
        }
        os::ipc::Decoder decoder{message.payload()};
        auto path = decode_path(decoder);
        if (!path) return os::ipc::send_rpc_error(slot.endpoint, message.header(), path.error());
        auto contents = decoder.read_bytes(max_storage_atomic_bytes);
        if (!contents) return os::ipc::send_rpc_error(slot.endpoint, message.header(), contents.error());
        auto end = decoder.require_end();
        if (!end) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), ipc_error(os::ipc::errors::protocol_violation));
        }
        auto result = atomic_replace_from_slot(path.value(), contents.value());
        if (!result) return os::ipc::send_rpc_error(slot.endpoint, message.header(), result.error());
        return os::ipc::send_rpc_response(slot.endpoint, message.header(), {});
    }
    case object_read_at_operation: {
        if (!is_file || !has_rights(slot.rights, file_rights::read)) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), storage_error(errors::access_denied));
        }
        os::ipc::Decoder decoder{message.payload()};
        auto offset = decoder.read_u64_le();
        if (!offset) return os::ipc::send_rpc_error(slot.endpoint, message.header(), offset.error());
        auto requested = decoder.read_u32_le();
        if (!requested) return os::ipc::send_rpc_error(slot.endpoint, message.header(), requested.error());
        auto end = decoder.require_end();
        if (!end) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), ipc_error(os::ipc::errors::protocol_violation));
        }
        if (requested.value() > max_storage_io_bytes) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), storage_error(errors::too_large));
        }

        auto data = receive_buffer.subspan(4U, static_cast<std::size_t>(requested.value()));
        auto read = slot.file.read_at(offset.value(), data);
        if (!read) return os::ipc::send_rpc_error(slot.endpoint, message.header(), read.error());

        os::ipc::Encoder length_encoder{receive_buffer.first(4U)};
        auto length_result = length_encoder.write_u32_le(static_cast<std::uint32_t>(read.value()));
        if (!length_result) {
            return os::ipc::send_rpc_error(slot.endpoint, message.header(), length_result.error());
        }
        return os::ipc::send_rpc_response(
            slot.endpoint,
            message.header(),
            receive_buffer.first(4U + read.value()));
    }
    case object_write_at_operation: {
        if (!is_file || !has_rights(slot.rights, file_rights::write)) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), storage_error(errors::access_denied));
        }
        os::ipc::Decoder decoder{message.payload()};
        auto offset = decoder.read_u64_le();
        if (!offset) return os::ipc::send_rpc_error(slot.endpoint, message.header(), offset.error());
        auto bytes = decoder.read_bytes(max_storage_io_bytes);
        if (!bytes) return os::ipc::send_rpc_error(slot.endpoint, message.header(), bytes.error());
        auto end = decoder.require_end();
        if (!end) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), ipc_error(os::ipc::errors::protocol_violation));
        }
        auto written = slot.file.write_at(offset.value(), bytes.value());
        if (!written) return os::ipc::send_rpc_error(slot.endpoint, message.header(), written.error());
        std::array<std::byte, 4U> response_storage{};
        os::ipc::Encoder encoder{response_storage};
        auto encoded = encoder.write_u32_le(static_cast<std::uint32_t>(written.value()));
        if (!encoded) return os::ipc::send_rpc_error(slot.endpoint, message.header(), encoded.error());
        return os::ipc::send_rpc_response(slot.endpoint, message.header(), encoder.written());
    }
    case object_size_operation: {
        if (!is_file || !has_rights(slot.rights, file_rights::stat)) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), storage_error(errors::access_denied));
        }
        if (!message.payload().empty()) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), ipc_error(os::ipc::errors::protocol_violation));
        }
        auto size = slot.file.size();
        if (!size) return os::ipc::send_rpc_error(slot.endpoint, message.header(), size.error());
        std::array<std::byte, 8U> response_storage{};
        os::ipc::Encoder encoder{response_storage};
        auto encoded = encoder.write_u64_le(size.value());
        if (!encoded) return os::ipc::send_rpc_error(slot.endpoint, message.header(), encoded.error());
        return os::ipc::send_rpc_response(slot.endpoint, message.header(), encoder.written());
    }
    case object_sync_operation: {
        if (!is_file || !has_rights(slot.rights, file_rights::sync)) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), storage_error(errors::access_denied));
        }
        if (!message.payload().empty()) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), ipc_error(os::ipc::errors::protocol_violation));
        }
        auto synced = slot.file.sync();
        if (!synced) return os::ipc::send_rpc_error(slot.endpoint, message.header(), synced.error());
        return os::ipc::send_rpc_response(slot.endpoint, message.header(), {});
    }
    default:
        return os::ipc::send_rpc_error(
            slot.endpoint,
            message.header(),
            os::core::make_error(
                os::core::ErrorDomain::service,
                os::core::errors::service::unknown_operation));
    }
}

} // namespace os::storage
