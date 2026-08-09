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

} // namespace os::ui
