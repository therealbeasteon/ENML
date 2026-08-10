#include <os/collection/session.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>
#include <os/ipc/wire.hpp>
#include <os/ui/error.hpp>

namespace os::collection {
namespace {

struct ContentRequest final {
    CollectionSessionId session {};
    os::ui::CollectionRevision revision {};
    os::ui::CollectionWindow window {};
};

[[nodiscard]] constexpr os::core::Error ipc_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ipc, code);
}

[[nodiscard]] constexpr bool has_flag(std::uint32_t flags, os::ipc::WireFlag flag) noexcept {
    return (flags & os::ipc::flag_value(flag)) != 0U;
}

[[nodiscard]] bool request_header_valid(const os::ipc::InboundMessage& message) noexcept {
    const auto& header = message.header();
    const bool known_operation =
        header.operation_id == collection_session_op_snapshot ||
        header.operation_id == collection_session_op_changes ||
        header.operation_id == collection_session_op_content;
    return has_flag(header.flags, os::ipc::WireFlag::request) &&
        !has_flag(header.flags, os::ipc::WireFlag::response) &&
        !has_flag(header.flags, os::ipc::WireFlag::event) &&
        !has_flag(header.flags, os::ipc::WireFlag::error) &&
        !has_flag(header.flags, os::ipc::WireFlag::oneway) &&
        !has_flag(header.flags, os::ipc::WireFlag::cancellable) &&
        !has_flag(header.flags, os::ipc::WireFlag::has_handles) &&
        header.service_id == application_collection_session_service_id &&
        known_operation && header.request_id.value() != 0U &&
        header.handle_count == 0U && message.handle_count() == 0U;
}

[[nodiscard]] os::core::Result<void> write_request_prefix(
    os::ipc::Encoder& encoder,
    std::size_t size) noexcept {
    auto result = encoder.write_u16_le(transport_version_v1);
    if (!result) return result.error();
    return encoder.write_u16_le(static_cast<std::uint16_t>(size));
}

[[nodiscard]] os::core::Result<void> read_request_prefix(
    os::ipc::Decoder& decoder,
    std::size_t expected_size) noexcept {
    auto version = decoder.read_u16_le();
    if (!version) return version.error();
    auto size = decoder.read_u16_le();
    if (!size) return size.error();
    if (version.value() != transport_version_v1 ||
        static_cast<std::size_t>(size.value()) != expected_size) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return {};
}

[[nodiscard]] os::core::Result<std::size_t> encode_session_request(
    CollectionSessionId session,
    os::core::MutableByteSpan output) noexcept {
    if (session.value() == 0U || output.size() < session_snapshot_request_size_v1) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    os::ipc::Encoder encoder{output};
    auto result = write_request_prefix(encoder, session_snapshot_request_size_v1);
    if (!result) return result.error();
    result = encoder.write_u64_le(session.value());
    if (!result) return result.error();
    return encoder.written().size();
}

[[nodiscard]] os::core::Result<CollectionSessionId> decode_session_request(
    os::core::ByteSpan payload) noexcept {
    if (payload.size() != session_snapshot_request_size_v1) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    os::ipc::Decoder decoder{payload};
    auto prefix = read_request_prefix(decoder, session_snapshot_request_size_v1);
    if (!prefix) return prefix.error();
    auto session = decoder.read_u64_le();
    if (!session) return session.error();
    auto end = decoder.require_end();
    if (!end || session.value() == 0U) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return CollectionSessionId{session.value()};
}

[[nodiscard]] os::core::Result<std::size_t> encode_changes_request(
    CollectionSessionId session,
    os::ui::CollectionRevision revision,
    os::core::MutableByteSpan output) noexcept {
    if (session.value() == 0U || revision.value() == 0U ||
        output.size() < session_changes_request_size_v1) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    os::ipc::Encoder encoder{output};
    auto result = write_request_prefix(encoder, session_changes_request_size_v1);
    if (!result) return result.error();
    result = encoder.write_u64_le(session.value());
    if (!result) return result.error();
    result = encoder.write_u64_le(revision.value());
    if (!result) return result.error();
    return encoder.written().size();
}

[[nodiscard]] os::core::Result<std::pair<CollectionSessionId, os::ui::CollectionRevision>>
decode_changes_request(os::core::ByteSpan payload) noexcept {
    if (payload.size() != session_changes_request_size_v1) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    os::ipc::Decoder decoder{payload};
    auto prefix = read_request_prefix(decoder, session_changes_request_size_v1);
    if (!prefix) return prefix.error();
    auto session = decoder.read_u64_le();
    if (!session) return session.error();
    auto revision = decoder.read_u64_le();
    if (!revision) return revision.error();
    auto end = decoder.require_end();
    if (!end || session.value() == 0U || revision.value() == 0U) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return std::pair{
        CollectionSessionId{session.value()},
        os::ui::CollectionRevision{revision.value()},
    };
}

[[nodiscard]] bool request_window_valid(const os::ui::CollectionWindow& window) noexcept {
    if (window.count > os::ui::max_materialized_collection_items ||
        window.first_index > os::ui::max_collection_items ||
        static_cast<std::uint64_t>(window.first_index) + window.count >
            os::ui::max_collection_items) {
        return false;
    }
    if (window.count == 0U) return true;
    return window.item_extent_q6 != 0U &&
        window.item_extent_q6 <= os::ui::max_logical_dimension_q6;
}

[[nodiscard]] os::core::Result<std::size_t> encode_content_request(
    CollectionSessionId session,
    os::ui::CollectionRevision revision,
    const os::ui::CollectionWindow& window,
    os::core::MutableByteSpan output) noexcept {
    if (session.value() == 0U || revision.value() == 0U ||
        !request_window_valid(window) || output.size() < session_content_request_size_v1) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    os::ipc::Encoder encoder{output};
    auto result = write_request_prefix(encoder, session_content_request_size_v1);
    if (!result) return result.error();
    result = encoder.write_u64_le(session.value());
    if (!result) return result.error();
    result = encoder.write_u64_le(revision.value());
    if (!result) return result.error();
    result = encoder.write_u32_le(window.first_index);
    if (!result) return result.error();
    result = encoder.write_u16_le(window.count);
    if (!result) return result.error();
    result = encoder.write_u32_le(std::bit_cast<std::uint32_t>(window.first_item_offset_q6));
    if (!result) return result.error();
    result = encoder.write_u32_le(window.item_extent_q6);
    if (!result) return result.error();
    result = encoder.write_u64_le(window.content_extent_q6);
    if (!result) return result.error();
    return encoder.written().size();
}

[[nodiscard]] os::core::Result<ContentRequest> decode_content_request(
    os::core::ByteSpan payload) noexcept {
    if (payload.size() != session_content_request_size_v1) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    os::ipc::Decoder decoder{payload};
    auto prefix = read_request_prefix(decoder, session_content_request_size_v1);
    if (!prefix) return prefix.error();
    auto session = decoder.read_u64_le();
    if (!session) return session.error();
    auto revision = decoder.read_u64_le();
    if (!revision) return revision.error();
    auto first_index = decoder.read_u32_le();
    if (!first_index) return first_index.error();
    auto count = decoder.read_u16_le();
    if (!count) return count.error();
    auto first_offset = decoder.read_u32_le();
    if (!first_offset) return first_offset.error();
    auto item_extent = decoder.read_u32_le();
    if (!item_extent) return item_extent.error();
    auto content_extent = decoder.read_u64_le();
    if (!content_extent) return content_extent.error();
    auto end = decoder.require_end();
    if (!end || session.value() == 0U || revision.value() == 0U) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }

    ContentRequest request{
        .session = CollectionSessionId{session.value()},
        .revision = os::ui::CollectionRevision{revision.value()},
        .window = os::ui::CollectionWindow{
            .first_index = first_index.value(),
            .count = count.value(),
            .first_item_offset_q6 = std::bit_cast<std::int32_t>(first_offset.value()),
            .item_extent_q6 = item_extent.value(),
            .content_extent_q6 = content_extent.value(),
        },
    };
    if (!request_window_valid(request.window)) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return request;
}

[[nodiscard]] os::core::Result<void> validate_window_against_snapshot(
    const os::ui::CollectionWindow& window,
    const os::ui::CollectionDataSnapshot& snapshot) noexcept {
    const std::uint64_t end =
        static_cast<std::uint64_t>(window.first_index) + window.count;
    if (end > snapshot.item_count) {
        return os::ui::ui_error(os::ui::errors::invalid_collection);
    }
    if (snapshot.item_count == 0U) {
        if (window.first_index != 0U || window.count != 0U ||
            window.content_extent_q6 != 0U) {
            return os::ui::ui_error(os::ui::errors::invalid_collection);
        }
        return {};
    }
    if (window.item_extent_q6 == 0U ||
        window.item_extent_q6 > os::ui::max_logical_dimension_q6) {
        return os::ui::ui_error(os::ui::errors::invalid_collection);
    }
    const std::uint64_t expected_extent =
        static_cast<std::uint64_t>(snapshot.item_count) * window.item_extent_q6;
    if (window.content_extent_q6 != expected_extent) {
        return os::ui::ui_error(os::ui::errors::invalid_collection);
    }
    return {};
}

} // namespace

bool CollectionSessionServer::valid() const noexcept {
    return session_.value() != 0U &&
        backend_.data.snapshot != nullptr &&
        backend_.data.item_key_at != nullptr &&
        backend_.data.item_content_at != nullptr &&
        backend_.changes.changes_since != nullptr;
}

os::core::Result<void> CollectionSessionServer::dispatch_once(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan scratch) noexcept {
    if (!valid() || !channel.valid()) {
        return os::core::make_error(
            os::core::ErrorDomain::service,
            os::core::errors::service::invalid_request);
    }

    auto received = channel.receive(scratch);
    if (!received) return received.error();
    auto message = std::move(received).value();
    if (!request_header_valid(message)) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    const auto request_header = message.header();

    if (request_header.operation_id == collection_session_op_snapshot) {
        auto requested_session = decode_session_request(message.payload());
        if (!requested_session) {
            return os::ipc::send_rpc_error(channel, request_header, requested_session.error());
        }
        if (requested_session.value() != session_) {
            return os::ipc::send_rpc_error(
                channel, request_header, ipc_error(os::ipc::errors::protocol_violation));
        }
        auto snapshot = os::ui::collection_data_snapshot(backend_.data);
        if (!snapshot) return os::ipc::send_rpc_error(channel, request_header, snapshot.error());
        auto encoded = encode_snapshot_v1(snapshot.value(), scratch);
        if (!encoded) return os::ipc::send_rpc_error(channel, request_header, encoded.error());
        return os::ipc::send_rpc_response(
            channel, request_header, {scratch.data(), encoded.value()});
    }

    if (request_header.operation_id == collection_session_op_changes) {
        auto request = decode_changes_request(message.payload());
        if (!request) return os::ipc::send_rpc_error(channel, request_header, request.error());
        if (request.value().first != session_) {
            return os::ipc::send_rpc_error(
                channel, request_header, ipc_error(os::ipc::errors::protocol_violation));
        }
        auto changes = os::ui::collection_changes_since(
            request.value().second, backend_.changes);
        if (!changes) return os::ipc::send_rpc_error(channel, request_header, changes.error());
        auto encoded = encode_change_set_v1(changes.value(), scratch);
        if (!encoded) return os::ipc::send_rpc_error(channel, request_header, encoded.error());
        return os::ipc::send_rpc_response(
            channel, request_header, {scratch.data(), encoded.value()});
    }

    auto request = decode_content_request(message.payload());
    if (!request) return os::ipc::send_rpc_error(channel, request_header, request.error());
    if (request.value().session != session_) {
        return os::ipc::send_rpc_error(
            channel, request_header, ipc_error(os::ipc::errors::protocol_violation));
    }
    auto snapshot = os::ui::collection_data_snapshot(backend_.data);
    if (!snapshot) return os::ipc::send_rpc_error(channel, request_header, snapshot.error());
    if (snapshot.value().revision != request.value().revision) {
        return os::ipc::send_rpc_error(
            channel,
            request_header,
            os::ui::ui_error(os::ui::errors::stale_collection_snapshot));
    }
    auto window_valid = validate_window_against_snapshot(request.value().window, snapshot.value());
    if (!window_valid) {
        return os::ipc::send_rpc_error(channel, request_header, window_valid.error());
    }
    auto window = os::ui::build_collection_content_window(
        request.value().window,
        snapshot.value(),
        backend_.data);
    if (!window) return os::ipc::send_rpc_error(channel, request_header, window.error());
    auto encoded = encode_content_window_v1(window.value(), scratch);
    if (!encoded) return os::ipc::send_rpc_error(channel, request_header, encoded.error());
    return os::ipc::send_rpc_response(
        channel, request_header, {scratch.data(), encoded.value()});
}

os::core::Result<os::ui::CollectionDataSnapshot> CollectionSessionClient::snapshot(
    os::core::MutableByteSpan scratch) noexcept {
    if (!valid()) return ipc_error(os::ipc::errors::protocol_violation);
    std::array<std::byte, session_snapshot_request_size_v1> request{};
    auto encoded = encode_session_request(session_, request);
    if (!encoded) return encoded.error();
    auto response = connection_.call(
        application_collection_session_service_id,
        collection_session_op_snapshot,
        {request.data(), encoded.value()},
        scratch);
    if (!response) return response.error();
    if (response.value().handle_count() != 0U) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return decode_snapshot_v1(response.value().payload());
}

os::core::Result<os::ui::CollectionChangeSet> CollectionSessionClient::changes_since(
    os::ui::CollectionRevision from_revision,
    os::core::MutableByteSpan scratch) noexcept {
    if (!valid()) return ipc_error(os::ipc::errors::protocol_violation);
    std::array<std::byte, session_changes_request_size_v1> request{};
    auto encoded = encode_changes_request(session_, from_revision, request);
    if (!encoded) return encoded.error();
    auto response = connection_.call(
        application_collection_session_service_id,
        collection_session_op_changes,
        {request.data(), encoded.value()},
        scratch);
    if (!response) return response.error();
    if (response.value().handle_count() != 0U) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    auto changes = decode_change_set_v1(response.value().payload());
    if (!changes) return changes.error();
    if (changes.value().from_revision != from_revision) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return changes.value();
}

os::core::Result<void> CollectionSessionClient::content_window(
    os::ui::CollectionRevision revision,
    const os::ui::CollectionWindow& window,
    os::ui::CollectionContentWindow& output,
    os::core::MutableByteSpan scratch) noexcept {
    if (!valid()) return ipc_error(os::ipc::errors::protocol_violation);
    std::array<std::byte, session_content_request_size_v1> request{};
    auto encoded = encode_content_request(session_, revision, window, request);
    if (!encoded) return encoded.error();
    auto response = connection_.call(
        application_collection_session_service_id,
        collection_session_op_content,
        {request.data(), encoded.value()},
        scratch);
    if (!response) return response.error();
    if (response.value().handle_count() != 0U) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    auto decoded = decode_content_window_v1(response.value().payload(), output);
    if (!decoded) return decoded.error();
    if (output.revision != revision ||
        output.window.first_index != window.first_index ||
        output.window.count != window.count ||
        output.window.first_item_offset_q6 != window.first_item_offset_q6 ||
        output.window.item_extent_q6 != window.item_extent_q6 ||
        output.window.content_extent_q6 != window.content_extent_q6) {
        output = {};
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return {};
}

} // namespace os::collection
