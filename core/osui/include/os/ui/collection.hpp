#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/ui/types.hpp>

namespace os::ui {

inline constexpr std::uint32_t max_collection_items = 1'000'000U;
inline constexpr std::uint8_t max_collection_overscan_items = 8U;
inline constexpr std::uint16_t max_materialized_collection_items =
    static_cast<std::uint16_t>(max_ui_children_per_node);

struct CollectionItemKeyTag;
using CollectionItemKey = os::core::StrongId<CollectionItemKeyTag, std::uint64_t>;

struct CollectionRevisionTag;
using CollectionRevision = os::core::StrongId<CollectionRevisionTag, std::uint64_t>;

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

// Stable identity is separate from a collection index. Insertions, removals
// and reordering may change an item's index while its key remains constant.
// This lets a materialized semantic node keep focus/accessibility identity
// when the logical collection mutates underneath the visible window.
struct CollectionRecycleRequest final {
    CollectionWindow window {};
    std::array<CollectionItemKey, max_materialized_collection_items> item_keys {};
    std::uint16_t key_count {0U};
};

struct CollectionRecycleBinding final {
    std::uint16_t slot {0U};
    std::uint32_t item_index {0U};
    CollectionItemKey item_key {};
    bool retained {false};
};

struct CollectionRecyclePlan final {
    std::array<CollectionRecycleBinding, max_materialized_collection_items> bindings {};
    std::uint16_t count {0U};
};

// Renderer/UI code materializes a collection from one immutable logical
// revision. A data source may mutate between frames, but every key lookup for
// a materialization pass carries the revision captured by snapshot(). A source
// that has advanced must reject stale revision lookups instead of mixing two
// logical collection states into one semantic window.
struct CollectionDataSnapshot final {
    CollectionRevision revision {};
    std::uint32_t item_count {0U};
};

using CollectionSnapshotFn = bool (*)(
    void* context,
    CollectionDataSnapshot& output) noexcept;

using CollectionItemKeyAtFn = bool (*)(
    void* context,
    CollectionRevision revision,
    std::uint32_t item_index,
    CollectionItemKey& output) noexcept;

// In-process backend seam only. The eventual app-facing/OSIDL protocol can be
// designed after these semantics stabilize; applications do not receive raw
// function pointers or implementation-owned container addresses.
struct CollectionDataSourceBackend final {
    void* context {nullptr};
    CollectionSnapshotFn snapshot {nullptr};
    CollectionItemKeyAtFn item_key_at {nullptr};
};

// Maps virtual items onto a stable, fixed pool of materialized semantic child
// slots. Keyed binding retains a slot by logical item identity even if that
// item's index changes. The legacy window-only overload derives identity from
// index and is retained only for non-mutating/index-stable collections.
class CollectionRecycler final {
public:
    [[nodiscard]] os::core::Result<CollectionRecyclePlan> bind(
        const CollectionRecycleRequest& request) noexcept;
    [[nodiscard]] os::core::Result<CollectionRecyclePlan> bind(
        const CollectionWindow& window) noexcept;
    void reset() noexcept;
    [[nodiscard]] std::size_t active_count() const noexcept;

private:
    struct Slot final {
        bool occupied {false};
        CollectionItemKey item_key {};
        std::uint32_t item_index {0U};
    };

    std::array<Slot, max_materialized_collection_items> slots_ {};
};

[[nodiscard]] os::core::Result<CollectionWindow> plan_collection_window(
    const CollectionWindowRequest& request) noexcept;

[[nodiscard]] os::core::Result<std::int32_t> collection_item_offset_q6(
    const CollectionWindow& window,
    std::uint32_t item_index) noexcept;

// Captures one bounded source revision. Revision zero and item counts beyond
// ENML's collection ceiling are rejected before layout or recycling begins.
[[nodiscard]] os::core::Result<CollectionDataSnapshot> collection_data_snapshot(
    CollectionDataSourceBackend backend) noexcept;

// Resolves stable keys for a materialized window against exactly one captured
// source revision. Backend refusal is treated as a stale snapshot; zero or
// duplicate keys are rejected as a malformed source contract.
[[nodiscard]] os::core::Result<CollectionRecycleRequest> build_collection_recycle_request(
    const CollectionWindow& window,
    const CollectionDataSnapshot& snapshot,
    CollectionDataSourceBackend backend) noexcept;

} // namespace os::ui
