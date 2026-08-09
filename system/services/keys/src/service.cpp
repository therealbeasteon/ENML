#include <os/keys/service.hpp>

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
#include <os/keys/error.hpp>

namespace os::keys {
namespace {

[[nodiscard]] constexpr os::core::Error ipc_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ipc, code);
}

[[nodiscard]] constexpr bool has_flag(std::uint32_t flags, os::ipc::WireFlag flag) noexcept {
    return (flags & os::ipc::flag_value(flag)) != 0U;
}

[[nodiscard]] os::core::Result<void>
validate_scratch(os::core::MutableByteSpan scratch) noexcept {
    if (scratch.size() < os::ipc::max_wire_packet_size) {
        return ipc_error(os::ipc::errors::buffer_too_small);
    }
    return {};
}

[[nodiscard]] os::core::Result<void>
encode_descriptor(os::ipc::Encoder& encoder, const KeyDescriptor& descriptor) noexcept {
    if (!descriptor.valid()) return key_error(errors::invalid_key);
    auto result = encoder.write_u64_le(descriptor.id.high);
    if (!result) return result.error();
    result = encoder.write_u64_le(descriptor.id.low);
    if (!result) return result.error();
    result = encoder.write_u32_le(descriptor.version);
    if (!result) return result.error();
    result = encoder.write_u32_le(static_cast<std::uint32_t>(descriptor.purpose));
    if (!result) return result.error();
    return encoder.write_u32_le(descriptor.rights);
}

[[nodiscard]] os::core::Result<KeyDescriptor>
decode_descriptor(os::core::ByteSpan payload) noexcept {
    os::ipc::Decoder decoder{payload};
    auto high = decoder.read_u64_le();
    if (!high) return high.error();
    auto low = decoder.read_u64_le();
    if (!low) return low.error();
    auto version = decoder.read_u32_le();
    if (!version) return version.error();
    auto purpose = decoder.read_u32_le();
    if (!purpose) return purpose.error();
    auto rights_value = decoder.read_u32_le();
    if (!rights_value) return rights_value.error();
    auto end = decoder.require_end();
    if (!end) return ipc_error(os::ipc::errors::protocol_violation);

    const auto key_purpose = static_cast<KeyPurpose>(purpose.value());
    const KeyDescriptor descriptor{
        .id = KeyId{high.value(), low.value()},
        .version = version.value(),
        .purpose = key_purpose,
        .rights = rights_value.value(),
    };
    if (!descriptor.valid() || !valid_purpose(key_purpose)) {
        return key_error(errors::invalid_key);
    }
    return descriptor;
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

[[nodiscard]] bool valid_object_request(const os::ipc::InboundMessage& message) noexcept {
    const auto& header = message.header();
    return has_flag(header.flags, os::ipc::WireFlag::request) &&
        !has_flag(header.flags, os::ipc::WireFlag::response) &&
        !has_flag(header.flags, os::ipc::WireFlag::event) &&
        !has_flag(header.flags, os::ipc::WireFlag::error) &&
        !has_flag(header.flags, os::ipc::WireFlag::oneway) &&
        !has_flag(header.flags, os::ipc::WireFlag::cancellable) &&
        header.service_id == key_object_service_id &&
        header.request_id.value() != 0U &&
        header.handle_count == 0U;
}

[[nodiscard]] os::core::Result<os::ipc::InboundMessage>
object_call(
    os::ipc::Channel& channel,
    std::uint64_t& next_request_id,
    std::uint32_t operation_id,
    os::core::ByteSpan payload,
    os::core::MutableByteSpan scratch) noexcept {
    auto scratch_result = validate_scratch(scratch);
    if (!scratch_result) return scratch_result.error();
    if (!channel.valid()) return ipc_error(os::ipc::errors::invalid_channel);
    if (next_request_id == 0U) return ipc_error(os::ipc::errors::request_id_exhausted);

    const os::core::RequestId request_id{next_request_id};
    if (next_request_id == std::numeric_limits<std::uint64_t>::max()) {
        next_request_id = 0U;
    } else {
        ++next_request_id;
    }

    const os::ipc::WireHeaderV1 request_header{
        .flags = os::ipc::flag_value(os::ipc::WireFlag::request),
        .service_id = key_object_service_id,
        .operation_id = operation_id,
        .request_id = request_id,
        .payload_size = static_cast<std::uint32_t>(payload.size()),
        .handle_count = 0U,
        .payload_checksum = 0U,
    };
    auto sent = channel.send(request_header, payload);
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
        header.service_id != key_object_service_id ||
        header.operation_id != operation_id ||
        header.request_id != request_id) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    if (has_flag(header.flags, os::ipc::WireFlag::error)) {
        if (header.handle_count != 0U) return ipc_error(os::ipc::errors::protocol_violation);
        os::core::Error remote{};
        auto decoded = os::ipc::decode_rpc_error(response.payload(), remote);
        if (!decoded) return decoded.error();
        return remote;
    }
    return response;
}

[[nodiscard]] KeyOwner owner_from_context(const os::ipc::RequestContext& context) noexcept {
    return KeyOwner{
        .principal = context.peer.principal,
        .user = context.peer.user,
    };
}

} // namespace

os::core::Result<KeyObjectHandle>
KeyObjectHandle::adopt(os::ipc::Channel channel, KeyDescriptor descriptor) noexcept {
    if (!channel.valid()) return ipc_error(os::ipc::errors::invalid_channel);
    if (!descriptor.valid()) return key_error(errors::invalid_key);
    return KeyObjectHandle{std::move(channel), descriptor};
}

os::core::Result<void>
KeyObjectHandle::destroy(os::core::MutableByteSpan scratch) noexcept {
    if ((descriptor_.rights & key_rights::destroy) == 0U) {
        return key_error(errors::access_denied);
    }
    auto call = object_call(
        channel_,
        next_request_id_,
        key_object_destroy_operation,
        {},
        scratch);
    if (!call) return call.error();
    auto response = std::move(call).value();
    if (response.handle_count() != 0U || !response.payload().empty()) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    channel_.close();
    descriptor_ = {};
    return {};
}

os::core::Result<KeyObjectHandle>
KeyClient::create_application_data_key(os::core::MutableByteSpan scratch) noexcept {
    if (connection_ == nullptr) return ipc_error(os::ipc::errors::invalid_channel);
    auto scratch_result = validate_scratch(scratch);
    if (!scratch_result) return scratch_result.error();

    os::ipc::Encoder encoder{scratch.first(os::ipc::max_inline_payload_size)};
    auto encoded = encoder.write_u32_le(
        static_cast<std::uint32_t>(KeyPurpose::application_data_aead));
    if (!encoded) return encoded.error();
    auto call = connection_->call(key_service_id, key_create_operation, encoder.written(), scratch);
    if (!call) return call.error();
    auto response = std::move(call).value();
    auto descriptor = decode_descriptor(response.payload());
    if (!descriptor) return descriptor.error();
    auto endpoint = take_single_endpoint(response);
    if (!endpoint) return endpoint.error();
    return KeyObjectHandle::adopt(std::move(endpoint).value(), descriptor.value());
}

os::core::Result<KeyObjectHandle>
KeyClient::open(KeyId id, os::core::MutableByteSpan scratch) noexcept {
    if (connection_ == nullptr) return ipc_error(os::ipc::errors::invalid_channel);
    if (!id.valid()) return key_error(errors::invalid_key);
    auto scratch_result = validate_scratch(scratch);
    if (!scratch_result) return scratch_result.error();

    os::ipc::Encoder encoder{scratch.first(os::ipc::max_inline_payload_size)};
    auto encoded = encoder.write_u64_le(id.high);
    if (!encoded) return encoded.error();
    encoded = encoder.write_u64_le(id.low);
    if (!encoded) return encoded.error();
    auto call = connection_->call(key_service_id, key_open_operation, encoder.written(), scratch);
    if (!call) return call.error();
    auto response = std::move(call).value();
    auto descriptor = decode_descriptor(response.payload());
    if (!descriptor) return descriptor.error();
    if (descriptor.value().id != id) return ipc_error(os::ipc::errors::protocol_violation);
    auto endpoint = take_single_endpoint(response);
    if (!endpoint) return endpoint.error();
    return KeyObjectHandle::adopt(std::move(endpoint).value(), descriptor.value());
}

os::core::Result<std::size_t> KeyService::allocate_slot() noexcept {
    for (std::size_t index = 0U; index < objects_.size(); ++index) {
        if (!objects_[index].occupied) return index;
    }
    return key_error(errors::registry_full);
}

void KeyService::clear_slot(std::size_t index) noexcept {
    if (index < objects_.size()) objects_[index] = ObjectSlot{};
}

void KeyService::clear_slots_for(KeyId id) noexcept {
    for (std::size_t index = 0U; index < objects_.size(); ++index) {
        if (objects_[index].occupied && objects_[index].descriptor.id == id) {
            clear_slot(index);
        }
    }
}

std::size_t KeyService::live_object_count() const noexcept {
    std::size_t count = 0U;
    for (const auto& object : objects_) {
        if (object.occupied) ++count;
    }
    return count;
}

os::core::Result<void>
KeyService::dispatch_once(os::core::MutableByteSpan receive_buffer, int timeout_ms) noexcept {
    if (endpoint_ == nullptr || identity_resolver_ == nullptr || registry_ == nullptr ||
        id_source_ == nullptr || !endpoint_->valid()) {
        return ipc_error(os::ipc::errors::invalid_channel);
    }
    auto scratch_result = validate_scratch(receive_buffer);
    if (!scratch_result) return scratch_result.error();
    if (timeout_ms < -1) return os::core::core_error(os::core::errors::core::invalid_argument);

    std::array<pollfd, max_key_objects + 1U> descriptors{};
    for (auto& descriptor : descriptors) {
        descriptor.fd = -1;
        descriptor.events = POLLIN;
        descriptor.revents = 0;
    }
    descriptors[0].fd = endpoint_->native_fd();
    for (std::size_t index = 0U; index < objects_.size(); ++index) {
        if (objects_[index].occupied) {
            descriptors[index + 1U].fd = objects_[index].endpoint.native_fd();
        }
    }

    int result = -1;
    do {
        result = ::poll(descriptors.data(), static_cast<nfds_t>(descriptors.size()), timeout_ms);
    } while (result < 0 && errno == EINTR);
    if (result < 0) return key_error(errors::provider_failure);
    if (result == 0) return {};

    if ((descriptors[0].revents & POLLIN) != 0) return dispatch_main(receive_buffer);
    if ((descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return ipc_error(os::ipc::errors::peer_died);
    }

    for (std::size_t index = 0U; index < objects_.size(); ++index) {
        const auto revents = descriptors[index + 1U].revents;
        if ((revents & POLLIN) != 0) return dispatch_object(index, receive_buffer);
        if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            clear_slot(index);
            return {};
        }
    }
    return {};
}

os::core::Result<void>
KeyService::dispatch_main(os::core::MutableByteSpan receive_buffer) noexcept {
    auto received = endpoint_->receive(receive_buffer);
    if (!received) return received.error();
    auto message = std::move(received).value();

    auto context = os::ipc::validate_rpc_request(message, key_service_id, *identity_resolver_);
    if (!context) return os::ipc::send_rpc_error(*endpoint_, message.header(), context.error());
    const KeyOwner owner = owner_from_context(context.value());

    KeyDescriptor descriptor{};
    bool created_new_key = false;
    switch (message.header().operation_id) {
    case key_create_operation: {
        os::ipc::Decoder decoder{message.payload()};
        auto purpose_raw = decoder.read_u32_le();
        if (!purpose_raw) return os::ipc::send_rpc_error(*endpoint_, message.header(), purpose_raw.error());
        auto end = decoder.require_end();
        if (!end) {
            return os::ipc::send_rpc_error(
                *endpoint_, message.header(), ipc_error(os::ipc::errors::protocol_violation));
        }
        const auto purpose = static_cast<KeyPurpose>(purpose_raw.value());
        if (!valid_purpose(purpose)) {
            return os::ipc::send_rpc_error(
                *endpoint_, message.header(), key_error(errors::unsupported_purpose));
        }

        auto id = id_source_->next();
        if (!id) return os::ipc::send_rpc_error(*endpoint_, message.header(), id.error());
        if (!id.value().valid()) {
            return os::ipc::send_rpc_error(
                *endpoint_, message.header(), key_error(errors::invalid_key));
        }
        auto created = registry_->create(owner, id.value(), purpose, key_rights::all);
        if (!created) return os::ipc::send_rpc_error(*endpoint_, message.header(), created.error());
        descriptor = created.value();
        created_new_key = true;
        break;
    }
    case key_open_operation: {
        os::ipc::Decoder decoder{message.payload()};
        auto high = decoder.read_u64_le();
        if (!high) return os::ipc::send_rpc_error(*endpoint_, message.header(), high.error());
        auto low = decoder.read_u64_le();
        if (!low) return os::ipc::send_rpc_error(*endpoint_, message.header(), low.error());
        auto end = decoder.require_end();
        if (!end) {
            return os::ipc::send_rpc_error(
                *endpoint_, message.header(), ipc_error(os::ipc::errors::protocol_violation));
        }
        auto described = registry_->describe(owner, KeyId{high.value(), low.value()});
        if (!described) return os::ipc::send_rpc_error(*endpoint_, message.header(), described.error());
        descriptor = described.value();
        break;
    }
    default:
        return os::ipc::send_rpc_error(
            *endpoint_,
            message.header(),
            os::core::make_error(
                os::core::ErrorDomain::service,
                os::core::errors::service::unknown_operation));
    }

    auto slot_index = allocate_slot();
    if (!slot_index) {
        if (created_new_key) os::core::discard_result(registry_->destroy(owner, descriptor.id));
        return os::ipc::send_rpc_error(*endpoint_, message.header(), slot_index.error());
    }
    auto pair_result = os::ipc::Channel::create_local_pair();
    if (!pair_result) {
        if (created_new_key) os::core::discard_result(registry_->destroy(owner, descriptor.id));
        return os::ipc::send_rpc_error(*endpoint_, message.header(), pair_result.error());
    }
    auto pair = std::move(pair_result).value();

    auto& slot = objects_[slot_index.value()];
    slot.occupied = true;
    slot.owner = owner;
    slot.descriptor = descriptor;
    slot.endpoint = std::move(pair[0]);

    std::array<std::byte, 32U> response_storage{};
    os::ipc::Encoder response_encoder{response_storage};
    auto encoded = encode_descriptor(response_encoder, descriptor);
    if (!encoded) {
        clear_slot(slot_index.value());
        if (created_new_key) os::core::discard_result(registry_->destroy(owner, descriptor.id));
        return os::ipc::send_rpc_error(*endpoint_, message.header(), encoded.error());
    }
    auto client_endpoint = pair[1].take_native_handle_for_transfer();
    std::span<const os::core::NativeHandle> handles{&client_endpoint, 1U};
    auto sent = os::ipc::send_rpc_response(
        *endpoint_, message.header(), response_encoder.written(), handles);
    if (!sent) {
        clear_slot(slot_index.value());
        if (created_new_key) os::core::discard_result(registry_->destroy(owner, descriptor.id));
    }
    return sent;
}

os::core::Result<void>
KeyService::dispatch_object(std::size_t index, os::core::MutableByteSpan receive_buffer) noexcept {
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
    if (message.header().operation_id != key_object_destroy_operation ||
        !message.payload().empty()) {
        return os::ipc::send_rpc_error(
            slot.endpoint,
            message.header(),
            os::core::make_error(
                os::core::ErrorDomain::service,
                os::core::errors::service::unknown_operation));
    }
    if ((slot.descriptor.rights & key_rights::destroy) == 0U) {
        return os::ipc::send_rpc_error(
            slot.endpoint, message.header(), key_error(errors::access_denied));
    }

    const KeyId id = slot.descriptor.id;
    auto destroyed = registry_->destroy(slot.owner, id);
    if (!destroyed) {
        return os::ipc::send_rpc_error(slot.endpoint, message.header(), destroyed.error());
    }

    auto sent = os::ipc::send_rpc_response(slot.endpoint, message.header(), {});
    clear_slots_for(id);
    return sent;
}

} // namespace os::keys
