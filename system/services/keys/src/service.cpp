#include <os/keys/service.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <poll.h>

#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>
#include <os/keys/error.hpp>

namespace os::keys {
namespace {

[[nodiscard]] constexpr os::core::Error ipc_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ipc, code);
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

struct CryptoRequestView final {
    os::core::ByteSpan aad {};
    os::core::ByteSpan data {};
};

[[nodiscard]] os::core::Result<CryptoRequestView>
decode_crypto_request(
    os::core::ByteSpan payload,
    std::size_t maximum_data_size) noexcept {
    os::ipc::Decoder decoder{payload};
    auto aad_size = decoder.read_u32_le();
    if (!aad_size) return aad_size.error();
    auto data_size = decoder.read_u32_le();
    if (!data_size) return data_size.error();

    const auto aad_count = static_cast<std::size_t>(aad_size.value());
    const auto data_count = static_cast<std::size_t>(data_size.value());
    if (aad_count > max_key_aad_bytes || data_count > maximum_data_size) {
        return key_error(errors::too_large);
    }

    auto aad = decoder.read_raw(aad_count);
    if (!aad) return aad.error();
    auto data = decoder.read_raw(data_count);
    if (!data) return data.error();
    auto end = decoder.require_end();
    if (!end) return ipc_error(os::ipc::errors::protocol_violation);
    return CryptoRequestView{aad.value(), data.value()};
}

[[nodiscard]] KeyOwner owner_from_context(const os::ipc::RequestContext& context) noexcept {
    return KeyOwner{
        .principal = context.peer.principal,
        .user = context.peer.user,
    };
}

} // namespace

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

void KeyService::update_slots_for(KeyDescriptor descriptor) noexcept {
    for (auto& slot : objects_) {
        if (slot.occupied && slot.descriptor.id == descriptor.id) {
            slot.descriptor = descriptor;
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

    // Poll only descriptors that are actually live. Passing the full fixed
    // object-capacity array to poll(2) makes nfds depend on policy capacity
    // rather than open resources and can exceed the service RLIMIT_NOFILE even
    // when almost every entry is -1. Compact polling keeps the runtime cost and
    // kernel-visible resource count proportional to active capabilities.
    std::array<pollfd, max_key_objects + 1U> descriptors{};
    std::array<std::size_t, max_key_objects> object_indices{};
    std::size_t descriptor_count = 1U;
    descriptors[0] = pollfd{
        .fd = endpoint_->native_fd(),
        .events = POLLIN,
        .revents = 0,
    };

    for (std::size_t index = 0U; index < objects_.size(); ++index) {
        if (!objects_[index].occupied) continue;
        descriptors[descriptor_count] = pollfd{
            .fd = objects_[index].endpoint.native_fd(),
            .events = POLLIN,
            .revents = 0,
        };
        object_indices[descriptor_count - 1U] = index;
        ++descriptor_count;
    }

    int result = -1;
    do {
        result = ::poll(
            descriptors.data(),
            static_cast<nfds_t>(descriptor_count),
            timeout_ms);
    } while (result < 0 && errno == EINTR);
    if (result < 0) return key_error(errors::provider_failure);
    if (result == 0) return {};

    if ((descriptors[0].revents & POLLIN) != 0) return dispatch_main(receive_buffer);
    if ((descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return ipc_error(os::ipc::errors::peer_died);
    }

    for (std::size_t descriptor_index = 1U;
         descriptor_index < descriptor_count;
         ++descriptor_index) {
        const std::size_t object_index = object_indices[descriptor_index - 1U];
        const auto revents = descriptors[descriptor_index].revents;
        if ((revents & POLLIN) != 0) return dispatch_object(object_index, receive_buffer);
        if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            clear_slot(object_index);
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
        if (!purpose_raw) {
            return os::ipc::send_rpc_error(*endpoint_, message.header(), purpose_raw.error());
        }
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
        if (!described) {
            return os::ipc::send_rpc_error(*endpoint_, message.header(), described.error());
        }
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
    slot.peer = context.value().peer;
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
    auto context = os::ipc::validate_rpc_request(
        message, key_object_service_id, *identity_resolver_);
    if (!context) {
        return os::ipc::send_rpc_error(slot.endpoint, message.header(), context.error());
    }
    if (context.value().peer != slot.peer) {
        return os::ipc::send_rpc_error(
            slot.endpoint, message.header(), key_error(errors::access_denied));
    }

    switch (message.header().operation_id) {
    case key_object_destroy_operation: {
        if (!message.payload().empty()) {
            return os::ipc::send_rpc_error(
                slot.endpoint,
                message.header(),
                ipc_error(os::ipc::errors::protocol_violation));
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

    case key_object_rotate_operation: {
        if (!message.payload().empty()) {
            return os::ipc::send_rpc_error(
                slot.endpoint,
                message.header(),
                ipc_error(os::ipc::errors::protocol_violation));
        }
        if ((slot.descriptor.rights & key_rights::rotate) == 0U) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), key_error(errors::access_denied));
        }

        auto rotated = registry_->rotate(slot.owner, slot.descriptor.id);
        if (!rotated) {
            return os::ipc::send_rpc_error(slot.endpoint, message.header(), rotated.error());
        }
        update_slots_for(rotated.value());

        std::array<std::byte, 32U> response_storage{};
        os::ipc::Encoder response_encoder{response_storage};
        auto encoded = encode_descriptor(response_encoder, rotated.value());
        if (!encoded) {
            return os::ipc::send_rpc_error(slot.endpoint, message.header(), encoded.error());
        }
        return os::ipc::send_rpc_response(
            slot.endpoint, message.header(), response_encoder.written());
    }

    case key_object_encrypt_operation: {
        if ((slot.descriptor.rights & key_rights::encrypt) == 0U) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), key_error(errors::access_denied));
        }
        auto request = decode_crypto_request(message.payload(), max_key_plaintext_bytes);
        if (!request) {
            return os::ipc::send_rpc_error(slot.endpoint, message.header(), request.error());
        }

        auto current = registry_->describe(slot.owner, slot.descriptor.id);
        if (!current) {
            return os::ipc::send_rpc_error(slot.endpoint, message.header(), current.error());
        }
        update_slots_for(current.value());

        const CiphertextHeaderV1 header{
            .profile = CryptoProfileId::aes_256_gcm_v1,
            .key_id = current.value().id,
            .key_version = current.value().version,
            .ciphertext_size = static_cast<std::uint32_t>(request.value().data.size()),
        };
        os::core::MutableByteSpan output{operation_buffer_.data(), operation_buffer_.size()};
        auto encoded_header = encode_ciphertext_header_v1(header, output);
        if (!encoded_header) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), encoded_header.error());
        }

        AeadNonce nonce{};
        AeadTag tag{};
        auto sealed = registry_->seal(
            slot.owner,
            current.value().id,
            current.value().version,
            header.profile,
            output.first(ciphertext_header_bytes),
            request.value().aad,
            request.value().data,
            output.subspan(ciphertext_fixed_overhead, request.value().data.size()),
            nonce,
            tag);
        if (!sealed) {
            return os::ipc::send_rpc_error(slot.endpoint, message.header(), sealed.error());
        }
        if (sealed.value() != request.value().data.size()) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), key_error(errors::provider_failure));
        }

        std::copy(
            nonce.bytes.begin(),
            nonce.bytes.end(),
            output.subspan(ciphertext_header_bytes, aead_nonce_bytes).begin());
        std::copy(
            tag.bytes.begin(),
            tag.bytes.end(),
            output.subspan(ciphertext_header_bytes + aead_nonce_bytes, aead_tag_bytes).begin());

        const std::size_t response_size = ciphertext_fixed_overhead + sealed.value();
        auto sent = os::ipc::send_rpc_response(
            slot.endpoint,
            message.header(),
            output.first(response_size));
        std::fill(
            operation_buffer_.begin(),
            operation_buffer_.begin() + static_cast<std::ptrdiff_t>(response_size),
            std::byte{0});
        return sent;
    }

    case key_object_decrypt_operation: {
        if ((slot.descriptor.rights & key_rights::decrypt) == 0U) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), key_error(errors::access_denied));
        }
        auto request = decode_crypto_request(message.payload(), max_ciphertext_envelope_bytes);
        if (!request) {
            return os::ipc::send_rpc_error(slot.endpoint, message.header(), request.error());
        }
        auto parsed = parse_ciphertext_envelope_v1(request.value().data);
        if (!parsed) {
            return os::ipc::send_rpc_error(slot.endpoint, message.header(), parsed.error());
        }
        if (parsed.value().header.key_id != slot.descriptor.id) {
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), key_error(errors::key_id_mismatch));
        }

        AeadNonce nonce{};
        AeadTag tag{};
        std::copy(parsed.value().nonce.begin(), parsed.value().nonce.end(), nonce.bytes.begin());
        std::copy(parsed.value().tag.begin(), parsed.value().tag.end(), tag.bytes.begin());

        os::core::MutableByteSpan output{operation_buffer_.data(), operation_buffer_.size()};
        auto opened = registry_->open(
            slot.owner,
            slot.descriptor.id,
            parsed.value().header.key_version,
            parsed.value().header.profile,
            parsed.value().authenticated_header,
            request.value().aad,
            nonce,
            tag,
            parsed.value().ciphertext,
            output.first(max_key_plaintext_bytes));
        if (!opened) {
            std::fill(operation_buffer_.begin(), operation_buffer_.end(), std::byte{0});
            return os::ipc::send_rpc_error(slot.endpoint, message.header(), opened.error());
        }
        if (opened.value() != parsed.value().ciphertext.size()) {
            std::fill(operation_buffer_.begin(), operation_buffer_.end(), std::byte{0});
            return os::ipc::send_rpc_error(
                slot.endpoint, message.header(), key_error(errors::provider_failure));
        }

        auto current = registry_->describe(slot.owner, slot.descriptor.id);
        if (current) update_slots_for(current.value());

        auto sent = os::ipc::send_rpc_response(
            slot.endpoint,
            message.header(),
            output.first(opened.value()));
        std::fill(
            operation_buffer_.begin(),
            operation_buffer_.begin() + static_cast<std::ptrdiff_t>(opened.value()),
            std::byte{0});
        return sent;
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

} // namespace os::keys