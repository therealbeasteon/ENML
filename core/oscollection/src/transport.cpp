#include <os/collection/transport.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>

namespace os::collection {
namespace {

inline constexpr std::uint8_t content_enabled = 1U << 0U;
inline constexpr std::uint8_t content_selected = 1U << 1U;
inline constexpr std::uint8_t known_content_flags = content_enabled | content_selected;

[[nodiscard]] constexpr os::core::Error ipc_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ipc, code);
}

[[nodiscard]] constexpr bool change_kind_valid(os::ui::CollectionChangeKind kind) noexcept {
    switch (kind) {
    case os::ui::CollectionChangeKind::insert:
    case os::ui::CollectionChangeKind::remove:
    case os::ui::CollectionChangeKind::move:
    case os::ui::CollectionChangeKind::update:
    case os::ui::CollectionChangeKind::reset:
        return true;
    }
    return false;
}

[[nodiscard]] os::core::Result<void> write_prefix(
    os::ipc::Encoder& encoder,
    std::size_t size) noexcept {
    if (size > std::numeric_limits<std::uint16_t>::max()) {
        return ipc_error(os::ipc::errors::oversized_message);
    }
    auto result = encoder.write_u16_le(transport_version_v1);
    if (!result) return result.error();
    return encoder.write_u16_le(static_cast<std::uint16_t>(size));
}

[[nodiscard]] os::core::Result<void> read_prefix(
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

[[nodiscard]] bool content_window_valid(
    const os::ui::CollectionContentWindow& value) noexcept {
    if (value.revision.value() == 0U ||
        value.count != value.window.count ||
        value.count > os::ui::max_materialized_collection_items ||
        value.window.first_index > os::ui::max_collection_items ||
        value.window.end_index() > os::ui::max_collection_items) {
        return false;
    }
    if (value.count != 0U && value.window.item_extent_q6 == 0U) return false;

    for (std::size_t index = 0U; index < value.count; ++index) {
        const auto& item = value.items[index];
        const std::uint32_t expected_index =
            value.window.first_index + static_cast<std::uint32_t>(index);
        if (item.item_index != expected_index || item.item_key.value() == 0U ||
            item.content.primary_label.empty() ||
            !os::ui::semantic_text_valid(item.content.primary_label) ||
            !os::ui::semantic_text_valid(item.content.secondary_label)) {
            return false;
        }
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (value.items[prior].item_key == item.item_key) return false;
        }
    }
    return true;
}

[[nodiscard]] os::core::Result<std::size_t> encoded_content_window_size(
    const os::ui::CollectionContentWindow& value) noexcept {
    if (!content_window_valid(value)) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    std::size_t size = content_window_header_size_v1;
    for (std::size_t index = 0U; index < value.count; ++index) {
        const auto& content = value.items[index].content;
        size += 21U + content.primary_label.view().size() +
            content.secondary_label.view().size();
    }
    if (size > max_content_window_size_v1 ||
        size > os::ipc::max_inline_payload_size ||
        size > std::numeric_limits<std::uint16_t>::max()) {
        return ipc_error(os::ipc::errors::oversized_message);
    }
    return size;
}

} // namespace

os::core::Result<std::size_t> encode_snapshot_v1(
    const os::ui::CollectionDataSnapshot& snapshot,
    os::core::MutableByteSpan output) noexcept {
    if (snapshot.revision.value() == 0U ||
        snapshot.item_count > os::ui::max_collection_items) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    os::ipc::Encoder encoder{output};
    auto result = write_prefix(encoder, snapshot_record_size_v1);
    if (!result) return result.error();
    result = encoder.write_u64_le(snapshot.revision.value());
    if (!result) return result.error();
    result = encoder.write_u32_le(snapshot.item_count);
    if (!result) return result.error();
    return encoder.written().size();
}

os::core::Result<os::ui::CollectionDataSnapshot> decode_snapshot_v1(
    os::core::ByteSpan payload) noexcept {
    if (payload.size() != snapshot_record_size_v1) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    os::ipc::Decoder decoder{payload};
    auto prefix = read_prefix(decoder, snapshot_record_size_v1);
    if (!prefix) return prefix.error();
    auto revision = decoder.read_u64_le();
    if (!revision) return revision.error();
    auto item_count = decoder.read_u32_le();
    if (!item_count) return item_count.error();
    auto end = decoder.require_end();
    if (!end || revision.value() == 0U ||
        item_count.value() > os::ui::max_collection_items) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return os::ui::CollectionDataSnapshot{
        .revision = os::ui::CollectionRevision{revision.value()},
        .item_count = item_count.value(),
    };
}

os::core::Result<std::size_t> encode_change_set_v1(
    const os::ui::CollectionChangeSet& changes,
    os::core::MutableByteSpan output) noexcept {
    if (!os::ui::collection_change_set_valid(changes)) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    const std::size_t size = change_set_header_size_v1 +
        static_cast<std::size_t>(changes.count) * change_record_size_v1;
    os::ipc::Encoder encoder{output};
    auto result = write_prefix(encoder, size);
    if (!result) return result.error();
    result = encoder.write_u64_le(changes.from_revision.value());
    if (!result) return result.error();
    result = encoder.write_u64_le(changes.to_revision.value());
    if (!result) return result.error();
    result = encoder.write_u32_le(changes.old_item_count);
    if (!result) return result.error();
    result = encoder.write_u32_le(changes.new_item_count);
    if (!result) return result.error();
    result = encoder.write_u8(changes.count);
    if (!result) return result.error();
    for (std::size_t index = 0U; index < changes.count; ++index) {
        const auto& change = changes.changes[index];
        result = encoder.write_u8(static_cast<std::uint8_t>(change.kind));
        if (!result) return result.error();
        result = encoder.write_u32_le(change.index);
        if (!result) return result.error();
        result = encoder.write_u32_le(change.count);
        if (!result) return result.error();
        result = encoder.write_u32_le(change.destination_index);
        if (!result) return result.error();
    }
    return encoder.written().size();
}

os::core::Result<os::ui::CollectionChangeSet> decode_change_set_v1(
    os::core::ByteSpan payload) noexcept {
    if (payload.size() < change_set_header_size_v1 ||
        payload.size() > max_change_set_size_v1) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    os::ipc::Decoder decoder{payload};
    auto version = decoder.read_u16_le();
    if (!version) return version.error();
    auto encoded_size = decoder.read_u16_le();
    if (!encoded_size) return encoded_size.error();
    auto from = decoder.read_u64_le();
    if (!from) return from.error();
    auto to = decoder.read_u64_le();
    if (!to) return to.error();
    auto old_count = decoder.read_u32_le();
    if (!old_count) return old_count.error();
    auto new_count = decoder.read_u32_le();
    if (!new_count) return new_count.error();
    auto count = decoder.read_u8();
    if (!count) return count.error();

    const std::size_t expected_size = change_set_header_size_v1 +
        static_cast<std::size_t>(count.value()) * change_record_size_v1;
    if (version.value() != transport_version_v1 ||
        static_cast<std::size_t>(encoded_size.value()) != payload.size() ||
        expected_size != payload.size() ||
        count.value() > os::ui::max_collection_changes) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }

    os::ui::CollectionChangeSet output{
        .from_revision = os::ui::CollectionRevision{from.value()},
        .to_revision = os::ui::CollectionRevision{to.value()},
        .old_item_count = old_count.value(),
        .new_item_count = new_count.value(),
        .count = count.value(),
    };
    for (std::size_t index = 0U; index < output.count; ++index) {
        auto kind = decoder.read_u8();
        if (!kind) return kind.error();
        auto item_index = decoder.read_u32_le();
        if (!item_index) return item_index.error();
        auto item_count = decoder.read_u32_le();
        if (!item_count) return item_count.error();
        auto destination = decoder.read_u32_le();
        if (!destination) return destination.error();
        const auto decoded_kind = static_cast<os::ui::CollectionChangeKind>(kind.value());
        if (!change_kind_valid(decoded_kind)) {
            return ipc_error(os::ipc::errors::protocol_violation);
        }
        output.changes[index] = os::ui::CollectionChange{
            .kind = decoded_kind,
            .index = item_index.value(),
            .count = item_count.value(),
            .destination_index = destination.value(),
        };
    }
    auto end = decoder.require_end();
    if (!end || !os::ui::collection_change_set_valid(output)) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return output;
}

os::core::Result<std::size_t> encode_content_window_v1(
    const os::ui::CollectionContentWindow& window,
    os::core::MutableByteSpan output) noexcept {
    auto size = encoded_content_window_size(window);
    if (!size) return size.error();
    os::ipc::Encoder encoder{output};
    auto result = write_prefix(encoder, size.value());
    if (!result) return result.error();
    result = encoder.write_u64_le(window.revision.value());
    if (!result) return result.error();
    result = encoder.write_u32_le(window.window.first_index);
    if (!result) return result.error();
    result = encoder.write_u16_le(window.window.count);
    if (!result) return result.error();
    result = encoder.write_u32_le(std::bit_cast<std::uint32_t>(window.window.first_item_offset_q6));
    if (!result) return result.error();
    result = encoder.write_u32_le(window.window.item_extent_q6);
    if (!result) return result.error();
    result = encoder.write_u64_le(window.window.content_extent_q6);
    if (!result) return result.error();
    result = encoder.write_u16_le(window.count);
    if (!result) return result.error();

    for (std::size_t index = 0U; index < window.count; ++index) {
        const auto& item = window.items[index];
        result = encoder.write_u32_le(item.item_index);
        if (!result) return result.error();
        result = encoder.write_u64_le(item.item_key.value());
        if (!result) return result.error();
        std::uint8_t flags = 0U;
        if (item.content.enabled) flags |= content_enabled;
        if (item.content.selected) flags |= content_selected;
        result = encoder.write_u8(flags);
        if (!result) return result.error();
        result = encoder.write_utf8(
            item.content.primary_label.view(), os::ui::max_semantic_text_bytes);
        if (!result) return result.error();
        result = encoder.write_utf8(
            item.content.secondary_label.view(), os::ui::max_semantic_text_bytes);
        if (!result) return result.error();
    }
    if (encoder.written().size() != size.value()) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return encoder.written().size();
}

os::core::Result<void> decode_content_window_v1(
    os::core::ByteSpan payload,
    os::ui::CollectionContentWindow& output) noexcept {
    if (payload.size() < content_window_header_size_v1 ||
        payload.size() > max_content_window_size_v1) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }

    output = {};
    os::ipc::Decoder decoder{payload};
    auto version = decoder.read_u16_le();
    if (!version) return version.error();
    auto encoded_size = decoder.read_u16_le();
    if (!encoded_size) return encoded_size.error();
    auto revision = decoder.read_u64_le();
    if (!revision) return revision.error();
    auto first_index = decoder.read_u32_le();
    if (!first_index) return first_index.error();
    auto window_count = decoder.read_u16_le();
    if (!window_count) return window_count.error();
    auto first_offset = decoder.read_u32_le();
    if (!first_offset) return first_offset.error();
    auto item_extent = decoder.read_u32_le();
    if (!item_extent) return item_extent.error();
    auto content_extent = decoder.read_u64_le();
    if (!content_extent) return content_extent.error();
    auto item_count = decoder.read_u16_le();
    if (!item_count) return item_count.error();

    if (version.value() != transport_version_v1 ||
        static_cast<std::size_t>(encoded_size.value()) != payload.size() ||
        revision.value() == 0U || window_count.value() != item_count.value() ||
        item_count.value() > os::ui::max_materialized_collection_items) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }

    output.revision = os::ui::CollectionRevision{revision.value()};
    output.window = os::ui::CollectionWindow{
        .first_index = first_index.value(),
        .count = window_count.value(),
        .first_item_offset_q6 = std::bit_cast<std::int32_t>(first_offset.value()),
        .item_extent_q6 = item_extent.value(),
        .content_extent_q6 = content_extent.value(),
    };
    output.count = item_count.value();

    for (std::size_t index = 0U; index < output.count; ++index) {
        auto item_index = decoder.read_u32_le();
        if (!item_index) return item_index.error();
        auto item_key = decoder.read_u64_le();
        if (!item_key) return item_key.error();
        auto flags = decoder.read_u8();
        if (!flags) return flags.error();
        if ((flags.value() & static_cast<std::uint8_t>(~known_content_flags)) != 0U) {
            return ipc_error(os::ipc::errors::protocol_violation);
        }
        auto primary = decoder.read_utf8(os::ui::max_semantic_text_bytes);
        if (!primary) return primary.error();
        auto secondary = decoder.read_utf8(os::ui::max_semantic_text_bytes);
        if (!secondary) return secondary.error();
        auto primary_text = os::ui::make_semantic_text(primary.value());
        if (!primary_text) return ipc_error(os::ipc::errors::protocol_violation);
        auto secondary_text = os::ui::make_semantic_text(secondary.value());
        if (!secondary_text) return ipc_error(os::ipc::errors::protocol_violation);

        output.items[index] = os::ui::CollectionPublishedItem{
            .item_index = item_index.value(),
            .item_key = os::ui::CollectionItemKey{item_key.value()},
            .content = os::ui::CollectionItemContent{
                .primary_label = primary_text.value(),
                .secondary_label = secondary_text.value(),
                .enabled = (flags.value() & content_enabled) != 0U,
                .selected = (flags.value() & content_selected) != 0U,
            },
        };
    }
    auto end = decoder.require_end();
    if (!end || !content_window_valid(output)) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    return {};
}

} // namespace os::collection
