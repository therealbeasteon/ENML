#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <os/core/error.hpp>
#include <os/ui/collection.hpp>
#include <os/ui/error.hpp>

namespace {

void expect_ui_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::ui);
    assert(error.code == code);
}

[[nodiscard]] os::ui::SemanticText text(std::string_view value) {
    auto result = os::ui::make_semantic_text(value);
    assert(result);
    return result.value();
}

struct Source final {
    std::uint64_t revision {10U};
    std::uint32_t count {3U};
    std::array<os::ui::CollectionItemKey, 4U> keys {
        os::ui::CollectionItemKey{101U},
        os::ui::CollectionItemKey{102U},
        os::ui::CollectionItemKey{103U},
        os::ui::CollectionItemKey{104U},
    };
    bool reject_content {false};
    bool empty_primary {false};
    bool bad_key_contract {false};
};

bool snapshot(
    void* context,
    os::ui::CollectionDataSnapshot& output) noexcept {
    if (context == nullptr) return false;
    const auto* source = static_cast<const Source*>(context);
    output = {
        .revision = os::ui::CollectionRevision{source->revision},
        .item_count = source->count,
    };
    return true;
}

bool key_at(
    void* context,
    os::ui::CollectionRevision revision,
    std::uint32_t index,
    os::ui::CollectionItemKey& output) noexcept {
    if (context == nullptr) return false;
    const auto* source = static_cast<const Source*>(context);
    if (revision.value() != source->revision || index >= source->count ||
        index >= source->keys.size()) {
        return false;
    }
    output = source->keys[index];
    return true;
}

bool content_at(
    void* context,
    os::ui::CollectionRevision revision,
    std::uint32_t index,
    os::ui::CollectionItemKey key,
    os::ui::CollectionItemContent& output) noexcept {
    if (context == nullptr) return false;
    const auto* source = static_cast<const Source*>(context);
    if (source->reject_content || revision.value() != source->revision ||
        index >= source->count || index >= source->keys.size()) {
        return false;
    }
    const os::ui::CollectionItemKey expected = source->keys[index];
    if (source->bad_key_contract || key != expected) return false;

    if (source->empty_primary) {
        output = {};
        return true;
    }

    switch (index) {
    case 0U:
        output = {
            .primary_label = text("Wi-Fi"),
            .secondary_label = text("Connected"),
            .enabled = true,
            .selected = true,
        };
        return true;
    case 1U:
        output = {
            .primary_label = text("Bluetooth"),
            .secondary_label = text("Off"),
            .enabled = true,
            .selected = false,
        };
        return true;
    case 2U:
        output = {
            .primary_label = text("Airplane mode"),
            .enabled = false,
            .selected = false,
        };
        return true;
    default:
        output = {
            .primary_label = text("Other"),
        };
        return true;
    }
}

} // namespace

int main() {
    Source source{};
    const os::ui::CollectionDataSourceBackend backend{
        .context = &source,
        .snapshot = snapshot,
        .item_key_at = key_at,
        .item_content_at = content_at,
    };

    auto captured = os::ui::collection_data_snapshot(backend);
    assert(captured);
    auto window = os::ui::plan_collection_window({
        .item_count = captured.value().item_count,
        .item_extent_q6 = os::ui::logical_from_dp(56U),
        .viewport_extent_q6 = os::ui::logical_from_dp(168U),
        .overscan_items = 0U,
    });
    assert(window);

    auto published = os::ui::build_collection_content_window(
        window.value(), captured.value(), backend);
    assert(published);
    assert(published.value().revision == captured.value().revision);
    assert(published.value().count == 3U);
    assert(published.value().items[0].item_index == 0U);
    assert(published.value().items[0].item_key == os::ui::CollectionItemKey{101U});
    assert(published.value().items[0].content.primary_label.view() == "Wi-Fi");
    assert(published.value().items[0].content.secondary_label.view() == "Connected");
    assert(published.value().items[0].content.selected);
    assert(published.value().items[1].content.primary_label.view() == "Bluetooth");
    assert(!published.value().items[1].content.selected);
    assert(!published.value().items[2].content.enabled);
    assert(published.value().items[2].content.secondary_label.empty());

    // Recycling and content publication must observe exactly the same stable
    // identities for a captured revision.
    auto recycle = os::ui::build_collection_recycle_request(
        window.value(), captured.value(), backend);
    assert(recycle);
    assert(recycle.value().key_count == published.value().count);
    for (std::size_t index = 0U; index < published.value().count; ++index) {
        assert(recycle.value().item_keys[index] == published.value().items[index].item_key);
    }

    // Advancing the source after capture invalidates the old materialization
    // instead of mixing old keys with new content.
    source.revision = 11U;
    auto stale = os::ui::build_collection_content_window(
        window.value(), captured.value(), backend);
    assert(!stale);
    expect_ui_error(stale.error(), os::ui::errors::stale_collection_snapshot);

    auto fresh = os::ui::collection_data_snapshot(backend);
    assert(fresh);
    source.reject_content = true;
    auto rejected = os::ui::build_collection_content_window(
        window.value(), fresh.value(), backend);
    assert(!rejected);
    expect_ui_error(rejected.error(), os::ui::errors::stale_collection_snapshot);
    source.reject_content = false;

    source.empty_primary = true;
    auto invalid_content = os::ui::build_collection_content_window(
        window.value(), fresh.value(), backend);
    assert(!invalid_content);
    expect_ui_error(
        invalid_content.error(),
        os::ui::errors::invalid_collection_content);
    source.empty_primary = false;

    auto no_content_backend = backend;
    no_content_backend.item_content_at = nullptr;
    auto missing = os::ui::build_collection_content_window(
        window.value(), fresh.value(), no_content_backend);
    assert(!missing);
    expect_ui_error(missing.error(), os::ui::errors::invalid_collection_source);

    // A malformed key source is rejected before content is allowed to publish.
    source.keys[2] = source.keys[1];
    auto duplicate_keys = os::ui::build_collection_content_window(
        window.value(), fresh.value(), backend);
    assert(!duplicate_keys);
    expect_ui_error(
        duplicate_keys.error(),
        os::ui::errors::invalid_collection_source);

    return 0;
}
