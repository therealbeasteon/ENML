#include <os/ui/collection.hpp>

#include <cstddef>
#include <cstdint>

#include <os/ui/error.hpp>

namespace os::ui {

os::core::Result<CollectionContentWindow> build_collection_content_window(
    const CollectionWindow& window,
    const CollectionDataSnapshot& snapshot,
    CollectionDataSourceBackend backend) noexcept {
    if (backend.item_content_at == nullptr) {
        return ui_error(errors::invalid_collection_source);
    }

    // Reuse the same revision/key validation as recycling so content cannot be
    // materialized for a different logical identity than the semantic slot it
    // will occupy.
    auto recycle = build_collection_recycle_request(window, snapshot, backend);
    if (!recycle) return recycle.error();

    CollectionContentWindow output{
        .revision = snapshot.revision,
        .window = window,
        .count = window.count,
    };

    for (std::uint32_t offset = 0U; offset < window.count; ++offset) {
        const std::size_t slot = static_cast<std::size_t>(offset);
        const std::uint32_t item_index = window.first_index + offset;
        const CollectionItemKey key = recycle.value().item_keys[slot];
        CollectionItemContent content{};
        if (!backend.item_content_at(
                backend.context,
                snapshot.revision,
                item_index,
                key,
                content)) {
            // A source advancing between key and content lookup must fail the
            // captured revision instead of constructing a mixed-state row.
            return ui_error(errors::stale_collection_snapshot);
        }
        if (content.primary_label.empty() ||
            !semantic_text_valid(content.primary_label) ||
            !semantic_text_valid(content.secondary_label)) {
            return ui_error(errors::invalid_collection_content);
        }

        output.items[slot] = CollectionPublishedItem{
            .item_index = item_index,
            .item_key = key,
            .content = content,
        };
    }

    return output;
}

} // namespace os::ui
