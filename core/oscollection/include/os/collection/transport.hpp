#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/ui/collection.hpp>
#include <os/ui/collection_changes.hpp>

namespace os::collection {

inline constexpr std::uint16_t transport_version_v1 = 1U;
inline constexpr std::size_t snapshot_record_size_v1 = 16U;
inline constexpr std::size_t change_record_size_v1 = 13U;
inline constexpr std::size_t change_set_header_size_v1 = 29U;
inline constexpr std::size_t max_change_set_size_v1 =
    change_set_header_size_v1 +
    os::ui::max_collection_changes * change_record_size_v1;

inline constexpr std::size_t content_window_header_size_v1 = 36U;
inline constexpr std::size_t max_content_item_size_v1 =
    4U + 8U + 1U + 4U + os::ui::max_semantic_text_bytes +
    4U + os::ui::max_semantic_text_bytes;
inline constexpr std::size_t max_content_window_size_v1 =
    content_window_header_size_v1 +
    os::ui::max_materialized_collection_items * max_content_item_size_v1;

static_assert(max_content_window_size_v1 < 64U * 1024U);

[[nodiscard]] os::core::Result<std::size_t> encode_snapshot_v1(
    const os::ui::CollectionDataSnapshot& snapshot,
    os::core::MutableByteSpan output) noexcept;

[[nodiscard]] os::core::Result<os::ui::CollectionDataSnapshot> decode_snapshot_v1(
    os::core::ByteSpan payload) noexcept;

[[nodiscard]] os::core::Result<std::size_t> encode_change_set_v1(
    const os::ui::CollectionChangeSet& changes,
    os::core::MutableByteSpan output) noexcept;

[[nodiscard]] os::core::Result<os::ui::CollectionChangeSet> decode_change_set_v1(
    os::core::ByteSpan payload) noexcept;

[[nodiscard]] os::core::Result<std::size_t> encode_content_window_v1(
    const os::ui::CollectionContentWindow& window,
    os::core::MutableByteSpan output) noexcept;

[[nodiscard]] os::core::Result<void> decode_content_window_v1(
    os::core::ByteSpan payload,
    os::ui::CollectionContentWindow& output) noexcept;

} // namespace os::collection
