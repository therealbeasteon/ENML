#pragma once

#include <array>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/ui/collection.hpp>

namespace os::ui {

inline constexpr std::uint8_t max_collection_changes = 16U;

enum class CollectionChangeKind : std::uint8_t {
    insert = 1U,
    remove = 2U,
    move = 3U,
    update = 4U,
    reset = 5U,
};

// Changes are ordered and interpreted sequentially. `index` is evaluated
// against the collection state produced by all preceding changes. For move,
// `destination_index` is evaluated after removing the source range.
struct CollectionChange final {
    CollectionChangeKind kind {CollectionChangeKind::update};
    std::uint32_t index {0U};
    std::uint32_t count {0U};
    std::uint32_t destination_index {0U};
};

struct CollectionChangeSet final {
    CollectionRevision from_revision {};
    CollectionRevision to_revision {};
    std::uint32_t old_item_count {0U};
    std::uint32_t new_item_count {0U};
    std::array<CollectionChange, max_collection_changes> changes {};
    std::uint8_t count {0U};
};

using CollectionChangesSinceFn = bool (*)(
    void* context,
    CollectionRevision from_revision,
    CollectionChangeSet& output) noexcept;

// Separate internal seam so the stable-key snapshot source does not need to
// become a broad app ABI. A future OSIDL protocol can preserve these semantics
// without exposing function pointers or implementation-owned containers.
struct CollectionChangeSourceBackend final {
    void* context {nullptr};
    CollectionChangesSinceFn changes_since {nullptr};
};

[[nodiscard]] bool collection_change_set_valid(
    const CollectionChangeSet& changes) noexcept;

// Fetches one bounded transition from a known revision. Backends must return a
// transition beginning exactly at the requested revision; malformed or
// unbounded transitions are rejected before they can drive recycler/layout
// invalidation.
[[nodiscard]] os::core::Result<CollectionChangeSet> collection_changes_since(
    CollectionRevision from_revision,
    CollectionChangeSourceBackend backend) noexcept;

// Conservative invalidation test for a currently materialized old-revision
// window. Structural edits before the end of the window count as affecting it
// because they may shift stable items even when the edited rows were above the
// viewport. Distant updates after the window can therefore avoid needless UI
// rematerialization and wakeups.
[[nodiscard]] os::core::Result<bool> collection_change_affects_window(
    const CollectionChangeSet& changes,
    const CollectionWindow& old_window) noexcept;

} // namespace os::ui
