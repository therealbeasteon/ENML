#include <os/ui/collection.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <os/ui/error.hpp>

namespace os::ui {
namespace {

[[nodiscard]] bool window_valid_for_recycling(const CollectionWindow& window) noexcept {
    if (window.count > max_materialized_collection_items) return false;
    if (window.first_index > max_collection_items) return false;
    const std::uint64_t end = static_cast<std::uint64_t>(window.first_index) + window.count;
    if (end > max_collection_items) return false;
    if (window.count == 0U) return true;
    return window.item_extent_q6 != 0U &&
        window.item_extent_q6 <= max_logical_dimension_q6;
}

} // namespace

os::core::Result<CollectionWindow> plan_collection_window(
    const CollectionWindowRequest& request) noexcept {
    if (request.item_count > max_collection_items ||
        request.viewport_extent_q6 == 0U ||
        request.viewport_extent_q6 > max_logical_dimension_q6 ||
        request.overscan_items > max_collection_overscan_items) {
        return ui_error(errors::invalid_collection);
    }

    if (request.item_count == 0U) {
        if (request.scroll_offset_q6 != 0U) return ui_error(errors::invalid_collection);
        return CollectionWindow{};
    }
    if (request.item_extent_q6 == 0U ||
        request.item_extent_q6 > max_logical_dimension_q6) {
        return ui_error(errors::invalid_collection);
    }

    const std::uint64_t content_extent =
        static_cast<std::uint64_t>(request.item_count) *
        static_cast<std::uint64_t>(request.item_extent_q6);
    const std::uint64_t viewport_extent = request.viewport_extent_q6;
    const std::uint64_t max_scroll =
        content_extent > viewport_extent ? content_extent - viewport_extent : 0U;
    if (request.scroll_offset_q6 > max_scroll) {
        return ui_error(errors::invalid_collection);
    }

    const std::uint64_t visible_first64 =
        request.scroll_offset_q6 / request.item_extent_q6;
    const std::uint64_t visible_end_offset =
        std::min(content_extent, request.scroll_offset_q6 + viewport_extent);
    const std::uint64_t visible_end64 =
        (visible_end_offset + request.item_extent_q6 - 1U) /
        request.item_extent_q6;

    const std::uint32_t visible_first = static_cast<std::uint32_t>(visible_first64);
    const std::uint32_t visible_end = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(visible_end64, request.item_count));
    const std::uint32_t overscan = request.overscan_items;
    const std::uint32_t first = visible_first > overscan ? visible_first - overscan : 0U;
    const std::uint32_t end = std::min<std::uint32_t>(
        request.item_count,
        visible_end + overscan);
    const std::uint32_t count = end >= first ? end - first : 0U;
    if (count > max_materialized_collection_items) {
        return ui_error(errors::collection_window_limit);
    }

    const std::int64_t first_offset =
        static_cast<std::int64_t>(static_cast<std::uint64_t>(first) * request.item_extent_q6) -
        static_cast<std::int64_t>(request.scroll_offset_q6);
    if (first_offset < std::numeric_limits<std::int32_t>::min() ||
        first_offset > std::numeric_limits<std::int32_t>::max()) {
        return ui_error(errors::invalid_collection);
    }

    return CollectionWindow{
        .first_index = first,
        .count = static_cast<std::uint16_t>(count),
        .first_item_offset_q6 = static_cast<std::int32_t>(first_offset),
        .item_extent_q6 = request.item_extent_q6,
        .content_extent_q6 = content_extent,
    };
}

os::core::Result<std::int32_t> collection_item_offset_q6(
    const CollectionWindow& window,
    std::uint32_t item_index) noexcept {
    if (window.count == 0U || window.item_extent_q6 == 0U ||
        item_index < window.first_index || item_index >= window.end_index()) {
        return ui_error(errors::invalid_collection);
    }

    const std::uint64_t relative =
        static_cast<std::uint64_t>(item_index - window.first_index) *
        window.item_extent_q6;
    const std::int64_t offset =
        static_cast<std::int64_t>(window.first_item_offset_q6) +
        static_cast<std::int64_t>(relative);
    if (offset < std::numeric_limits<std::int32_t>::min() ||
        offset > std::numeric_limits<std::int32_t>::max()) {
        return ui_error(errors::invalid_collection);
    }
    return static_cast<std::int32_t>(offset);
}

os::core::Result<CollectionRecyclePlan> CollectionRecycler::bind(
    const CollectionWindow& window) noexcept {
    if (!window_valid_for_recycling(window)) {
        return ui_error(errors::invalid_collection);
    }

    const std::uint64_t first = window.first_index;
    const std::uint64_t end = first + window.count;
    for (auto& slot : slots_) {
        if (!slot.occupied) continue;
        const std::uint64_t item = slot.item_index;
        if (item < first || item >= end) slot = Slot{};
    }

    CollectionRecyclePlan plan{};
    plan.count = window.count;
    for (std::uint32_t offset = 0U; offset < window.count; ++offset) {
        const std::uint32_t item_index = window.first_index + offset;
        std::size_t slot_index = slots_.size();
        bool retained = false;

        for (std::size_t index = 0U; index < slots_.size(); ++index) {
            if (slots_[index].occupied && slots_[index].item_index == item_index) {
                slot_index = index;
                retained = true;
                break;
            }
        }
        if (!retained) {
            for (std::size_t index = 0U; index < slots_.size(); ++index) {
                if (!slots_[index].occupied) {
                    slot_index = index;
                    slots_[index] = Slot{.occupied = true, .item_index = item_index};
                    break;
                }
            }
        }
        if (slot_index == slots_.size()) {
            return ui_error(errors::collection_window_limit);
        }

        plan.bindings[offset] = CollectionRecycleBinding{
            .slot = static_cast<std::uint16_t>(slot_index),
            .item_index = item_index,
            .retained = retained,
        };
    }
    return plan;
}

void CollectionRecycler::reset() noexcept {
    for (auto& slot : slots_) slot = Slot{};
}

std::size_t CollectionRecycler::active_count() const noexcept {
    std::size_t count = 0U;
    for (const auto& slot : slots_) {
        if (slot.occupied) ++count;
    }
    return count;
}

} // namespace os::ui
