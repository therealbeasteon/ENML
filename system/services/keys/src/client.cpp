#include <os/keys/service.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

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

} // namespace

os::core::Result<KeyObjectHandle>
KeyObjectHandle::adopt(os::ipc::Channel channel, KeyDescriptor descriptor) noexcept {
    if (!channel.valid()) return ipc_error(os::ipc::errors::invalid_channel);
    if (!descriptor.valid()) return key_error(errors::invalid_key);
    return KeyObjectHandle{std::move(channel), descriptor};
}

os::core::Result<std::size_t>
KeyObjectHandle::encrypt(
    os::core::ByteSpan plaintext,
    os::core::ByteSpan aad,
    os::core::MutableByteSpan envelope_output,
    os::core::MutableByteSpan scratch) noexcept {
    if ((descriptor_.rights & key_rights::encrypt) == 0U) {
        return key_error(errors::access_denied);
    }
    if (plaintext.size() > max_key_plaintext_bytes || aad.size() > max_key_aad_bytes) {
        return key_error(errors::too_large);
    }
    const std::size_t expected_size = ciphertext_fixed_overhead + plaintext.size();
    if (envelope_output.size() < expected_size) {
        return key_error(errors::output_too_small);
    }
    auto scratch_result = validate_scratch(scratch);
    if (!scratch_result) return scratch_result.error();

    os::ipc::Encoder encoder{scratch.first(os::ipc::max_inline_payload_size)};
    auto encoded = encoder.write_u32_le(static_cast<std::uint32_t>(aad.size()));
    if (!encoded) return encoded.error();
    encoded = encoder.write_u32_le(static_cast<std::uint32_t>(plaintext.size()));
    if (!encoded) return encoded.error();
    encoded = encoder.write_raw(aad);
    if (!encoded) return encoded.error();
    encoded = encoder.write_raw(plaintext);
    if (!encoded) return encoded.error();

    auto call = object_call(
        channel_,
        next_request_id_,
        key_object_encrypt_operation,
        encoder.written(),
        scratch);
    if (!call) return call.error();
    auto response = std::move(call).value();
    if (response.handle_count() != 0U || response.payload().size() != expected_size) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }

    auto parsed = parse_ciphertext_envelope_v1(response.payload());
    if (!parsed) return parsed.error();
    if (parsed.value().header.key_id != descriptor_.id) {
        return key_error(errors::key_id_mismatch);
    }
    if (parsed.value().header.key_version < descriptor_.version) {
        return key_error(errors::key_version_mismatch);
    }
    descriptor_.version = parsed.value().header.key_version;
    std::copy(response.payload().begin(), response.payload().end(), envelope_output.begin());
    return response.payload().size();
}

os::core::Result<std::size_t>
KeyObjectHandle::decrypt(
    os::core::ByteSpan envelope,
    os::core::ByteSpan aad,
    os::core::MutableByteSpan plaintext_output,
    os::core::MutableByteSpan scratch) noexcept {
    if ((descriptor_.rights & key_rights::decrypt) == 0U) {
        return key_error(errors::access_denied);
    }
    if (aad.size() > max_key_aad_bytes || envelope.size() > max_ciphertext_envelope_bytes) {
        return key_error(errors::too_large);
    }
    auto parsed = parse_ciphertext_envelope_v1(envelope);
    if (!parsed) return parsed.error();
    if (parsed.value().header.key_id != descriptor_.id) {
        return key_error(errors::key_id_mismatch);
    }
    if (plaintext_output.size() < parsed.value().ciphertext.size()) {
        return key_error(errors::output_too_small);
    }
    auto scratch_result = validate_scratch(scratch);
    if (!scratch_result) return scratch_result.error();

    os::ipc::Encoder encoder{scratch.first(os::ipc::max_inline_payload_size)};
    auto encoded = encoder.write_u32_le(static_cast<std::uint32_t>(aad.size()));
    if (!encoded) return encoded.error();
    encoded = encoder.write_u32_le(static_cast<std::uint32_t>(envelope.size()));
    if (!encoded) return encoded.error();
    encoded = encoder.write_raw(aad);
    if (!encoded) return encoded.error();
    encoded = encoder.write_raw(envelope);
    if (!encoded) return encoded.error();

    auto call = object_call(
        channel_,
        next_request_id_,
        key_object_decrypt_operation,
        encoder.written(),
        scratch);
    if (!call) return call.error();
    auto response = std::move(call).value();
    if (response.handle_count() != 0U ||
        response.payload().size() != parsed.value().ciphertext.size()) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    if (parsed.value().header.key_version > descriptor_.version) {
        descriptor_.version = parsed.value().header.key_version;
    }
    std::copy(response.payload().begin(), response.payload().end(), plaintext_output.begin());
    return response.payload().size();
}

os::core::Result<KeyDescriptor>
KeyObjectHandle::rotate(os::core::MutableByteSpan scratch) noexcept {
    if ((descriptor_.rights & key_rights::rotate) == 0U) {
        return key_error(errors::access_denied);
    }
    auto call = object_call(
        channel_,
        next_request_id_,
        key_object_rotate_operation,
        {},
        scratch);
    if (!call) return call.error();
    auto response = std::move(call).value();
    if (response.handle_count() != 0U) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    auto rotated = decode_descriptor(response.payload());
    if (!rotated) return rotated.error();
    if (rotated.value().id != descriptor_.id ||
        rotated.value().purpose != descriptor_.purpose ||
        rotated.value().rights != descriptor_.rights ||
        rotated.value().version <= descriptor_.version) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    descriptor_ = rotated.value();
    return descriptor_;
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

} // namespace os::keys
