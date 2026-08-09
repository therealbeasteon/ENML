#include <os/ui/collection_changes.hpp>

#include <cstddef>
#include <cstdint>

#include <os/ui/error.hpp>

namespace os::ui {
namespace {

[[nodiscard]] constexpr bool kind_valid(CollectionChangeKind kind) noexcept {
    switch (kind) {
    case CollectionChangeKind::insert:
    case CollectionChangeKind::remove:
    case CollectionChangeKind::move:
    case CollectionChangeKind::update:
    case CollectionChangeKind::reset:
        return true;
    }
    return false;
}

[[nodiscard]] bool window_valid(
    const CollectionWindow& window,
    std::uint32_t old_item_count) noexcept {
    if (window.count > max_materialized_collection_items ||
        window.first_index > old_item_count) {
        return false;
    }
    const std::uint64_t end =
        static_cast<std::uint64_t>(window.first_index) + window.count;
    if (end > old_item_count) return false;
    if (window.count == 0U) return true;
    return window.item_extent_q6 != 0U &&
        window.item_extent_q6 <= max_logical_dimension_q6;
}

[[nodiscard]] bool apply_change(
    const CollectionChange& change,
    std::uint32_t& current_count,
    std::uint32_t final_count,
    bool reset_is_only_change) noexcept {
    if (!kind_valid(change.kind)) return false;

    switch (change.kind) {
    case CollectionChangeKind::insert: {
        if (change.count == 0U || change.destination_index != 0U ||
            change.index > current_count) {
            return false;
        }
        const std::uint64_t expanded =
            static_cast<std::uint64_t>(current_count) + change.count;
        if (expanded > max_collection_items) return false;
        current_count = static_cast<std::uint32_t>(expanded);
        return true;
    }
    case CollectionChangeKind::remove: {
        if (change.count == 0U || change.destination_index != 0U) return false;
        const std::uint64_t end =
            static_cast<std::uint64_t>(change.index) + change.count;
        if (end > current_count) return false;
        current_count -= change.count;
        return true;
    }
    case CollectionChangeKind::move: {
        if (change.count == 0U) return false;
        const std::uint64_t source_end =
            static_cast<std::uint64_t>(change.index) + change.count;
        if (source_end > current_count) return false;
        const std::uint32_t count_after_remove = current_count - change.count;
        return change.destination_index <= count_after_remove;
    }
    case CollectionChangeKind::update: {
        if (change.count == 0U || change.destination_index != 0U) return false;
        const std::uint64_t end =
            static_cast<std::uint64_t>(change.index) + change.count;
        return end <= current_count;
    }
    case CollectionChangeKind::reset:
        if (!reset_is_only_change || change.index != 0U || change.count != 0U ||
            change.destination_index != 0U) {
            return false;
        }
        current_count = final_count;
        return true;
    }
    return false;
}

} // namespace

bool collection_change_set_valid(const CollectionChangeSet& changes) noexcept {
    if (changes.from_revision.value() == 0U || changes.to_revision.value() == 0U ||
        changes.from_revision == changes.to_revision ||
        changes.old_item_count > max_collection_items ||
        changes.new_item_count > max_collection_items ||
        changes.count == 0U || changes.count > changes.changes.size()) {
        return false;
    }

    std::uint32_t current_count = changes.old_item_count;
    const bool reset_is_only_change = changes.count == 1U;
    for (std::size_t index = 0U; index < changes.count; ++index) {
        if (!apply_change(
                changes.changes[index],
                current_count,
                changes.new_item_count,
                reset_is_only_change)) {
            return false;
        }
    }
    return current_count == changes.new_item_count;
}

os::core::Result<CollectionChangeSet> collection_changes_since(
    CollectionRevision from_revision,
    CollectionChangeSourceBackend backend) noexcept {
    if (from_revision.value() == 0U) {
        return ui_error(errors::invalid_collection_change);
    }
    if (backend.changes_since == nullptr) {
        return ui_error(errors::collection_change_source_failed);
    }

    CollectionChangeSet changes {};
    if (!backend.changes_since(backend.context, from_revision, changes)) {
        return ui_error(errors::collection_change_source_failed);
    }
    if (changes.from_revision != from_revision || !collection_change_set_valid(changes)) {
        return ui_error(errors::invalid_collection_change);
    }
    return changes;
}

os::core::Result<bool> collection_change_affects_window(
    const CollectionChangeSet& changes,
    const CollectionWindow& old_window) noexcept {
    if (!collection_change_set_valid(changes) ||
        !window_valid(old_window, changes.old_item_count)) {
        return ui_error(errors::invalid_collection_change);
    }
    if (old_window.count == 0U) return false;

    const std::uint64_t window_start = old_window.first_index;
    const std::uint64_t window_end =
        static_cast<std::uint64_t>(old_window.first_index) + old_window.count;

    for (std::size_t index = 0U; index < changes.count; ++index) {
        const CollectionChange& change = changes.changes[index];
        switch (change.kind) {
        case CollectionChangeKind::insert:
        case CollectionChangeKind::remove:
            if (static_cast<std::uint64_t>(change.index) < window_end) return true;
            break;
        case CollectionChangeKind::move:
            if (static_cast<std::uint64_t>(change.index) < window_end ||
                static_cast<std::uint64_t>(change.destination_index) < window_end) {
                return true;
            }
            break;
        case CollectionChangeKind::update: {
            const std::uint64_t update_start = change.index;
            const std::uint64_t update_end = update_start + change.count;
            if (update_start < window_end && update_end > window_start) return true;
            break;
        }
        case CollectionChangeKind::reset:
            return true;
        }
    }
    return false;
}

} // namespace os::ui
