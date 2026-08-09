#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/ui/types.hpp>

namespace os::ui {

inline constexpr std::uint32_t max_collection_items = 1'000'000U;
inline constexpr std::uint8_t max_collection_overscan_items = 8U;
inline constexpr std::uint16_t max_materialized_collection_items =
    static_cast<std::uint16_t>(max_ui_children_per_node);

struct CollectionWindowRequest final {
    std::uint32_t item_count {0U};
    std::uint32_t item_extent_q6 {0U};
    std::uint64_t scroll_offset_q6 {0U};
    std::uint32_t viewport_extent_q6 {0U};
    std::uint8_t overscan_items {2U};
};

// A virtualized list owns semantic nodes only for this bounded window. The
// first item's position is relative to the visible viewport, so callers never
// need huge logical coordinates even for a long collection.
struct CollectionWindow final {
    std::uint32_t first_index {0U};
    std::uint16_t count {0U};
    std::int32_t first_item_offset_q6 {0};
    std::uint32_t item_extent_q6 {0U};
    std::uint64_t content_extent_q6 {0U};

    [[nodiscard]] constexpr std::uint32_t end_index() const noexcept {
        return first_index + static_cast<std::uint32_t>(count);
    }
};

[[nodiscard]] os::core::Result<CollectionWindow> plan_collection_window(
    const CollectionWindowRequest& request) noexcept;

[[nodiscard]] os::core::Result<std::int32_t> collection_item_offset_q6(
    const CollectionWindow& window,
    std::uint32_t item_index) noexcept;

} // namespace os::ui
