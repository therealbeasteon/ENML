#include <os/ui/collection.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/ui/error.hpp>

namespace {

void expect_ui_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::ui);
    assert(error.code == code);
}

[[nodiscard]] std::uint16_t slot_for(
    const os::ui::CollectionRecyclePlan& plan,
    std::uint32_t item_index) {
    for (std::uint16_t index = 0U; index < plan.count; ++index) {
        if (plan.bindings[index].item_index == item_index) return plan.bindings[index].slot;
    }
    return os::ui::max_materialized_collection_items;
}

[[nodiscard]] bool retained_item(
    const os::ui::CollectionRecyclePlan& plan,
    std::uint32_t item_index) {
    for (std::uint16_t index = 0U; index < plan.count; ++index) {
        if (plan.bindings[index].item_index == item_index) return plan.bindings[index].retained;
    }
    return false;
}

[[nodiscard]] std::uint16_t slot_for_key(
    const os::ui::CollectionRecyclePlan& plan,
    os::ui::CollectionItemKey key) {
    for (std::uint16_t index = 0U; index < plan.count; ++index) {
        if (plan.bindings[index].item_key == key) return plan.bindings[index].slot;
    }
    return os::ui::max_materialized_collection_items;
}

[[nodiscard]] bool retained_key(
    const os::ui::CollectionRecyclePlan& plan,
    os::ui::CollectionItemKey key) {
    for (std::uint16_t index = 0U; index < plan.count; ++index) {
        if (plan.bindings[index].item_key == key) return plan.bindings[index].retained;
    }
    return false;
}

struct TestCollectionSource final {
    std::uint64_t revision {1U};
    std::uint32_t item_count {0U};
    std::array<os::ui::CollectionItemKey, 8U> keys {};
};

bool snapshot_source(
    void* context,
    os::ui::CollectionDataSnapshot& output) noexcept {
    if (context == nullptr) return false;
    const auto* source = static_cast<const TestCollectionSource*>(context);
    output = os::ui::CollectionDataSnapshot{
        .revision = os::ui::CollectionRevision{source->revision},
        .item_count = source->item_count,
    };
    return true;
}

bool source_key_at(
    void* context,
    os::ui::CollectionRevision revision,
    std::uint32_t item_index,
    os::ui::CollectionItemKey& output) noexcept {
    if (context == nullptr) return false;
    const auto* source = static_cast<const TestCollectionSource*>(context);
    if (revision.value() != source->revision || item_index >= source->item_count ||
        item_index >= source->keys.size()) {
        return false;
    }
    output = source->keys[item_index];
    return true;
}

} // namespace

int main() {
    auto top = os::ui::plan_collection_window(os::ui::CollectionWindowRequest{
        .item_count = 10'000U,
        .item_extent_q6 = os::ui::logical_from_dp(56U),
        .scroll_offset_q6 = 0U,
        .viewport_extent_q6 = os::ui::logical_from_dp(640U),
        .overscan_items = 2U,
    });
    assert(top);
    assert(top.value().first_index == 0U);
    assert(top.value().count > 0U);
    assert(top.value().count <= os::ui::max_materialized_collection_items);
    assert(top.value().first_item_offset_q6 == 0);
    assert(top.value().content_extent_q6 ==
        static_cast<std::uint64_t>(10'000U) * os::ui::logical_from_dp(56U));

    const std::uint64_t deep_scroll =
        static_cast<std::uint64_t>(5'000U) * os::ui::logical_from_dp(56U) +
        os::ui::logical_from_dp(12U);
    auto deep = os::ui::plan_collection_window(os::ui::CollectionWindowRequest{
        .item_count = 10'000U,
        .item_extent_q6 = os::ui::logical_from_dp(56U),
        .scroll_offset_q6 = deep_scroll,
        .viewport_extent_q6 = os::ui::logical_from_dp(640U),
        .overscan_items = 2U,
    });
    assert(deep);
    assert(deep.value().first_index == 4'998U);
    assert(deep.value().count <= os::ui::max_materialized_collection_items);
    assert(deep.value().first_item_offset_q6 < 0);

    auto visible_offset = os::ui::collection_item_offset_q6(deep.value(), 5'000U);
    assert(visible_offset);
    assert(visible_offset.value() == -static_cast<std::int32_t>(os::ui::logical_from_dp(12U)));

    auto outside_window = os::ui::collection_item_offset_q6(deep.value(), 4'997U);
    assert(!outside_window);
    expect_ui_error(outside_window.error(), os::ui::errors::invalid_collection);

    // Recycle a fixed semantic child pool instead of allocating one child per
    // logical list item. Overlap across a one-row scroll keeps the same slots.
    os::ui::CollectionRecycler recycler{};
    auto first_plan = recycler.bind(top.value());
    assert(first_plan);
    assert(first_plan.value().count == top.value().count);
    assert(recycler.active_count() == top.value().count);
    for (std::uint16_t index = 0U; index < first_plan.value().count; ++index) {
        assert(!first_plan.value().bindings[index].retained);
    }

    auto one_row_down = os::ui::plan_collection_window(os::ui::CollectionWindowRequest{
        .item_count = 10'000U,
        .item_extent_q6 = os::ui::logical_from_dp(56U),
        .scroll_offset_q6 = os::ui::logical_from_dp(56U),
        .viewport_extent_q6 = os::ui::logical_from_dp(640U),
        .overscan_items = 2U,
    });
    assert(one_row_down);
    auto second_plan = recycler.bind(one_row_down.value());
    assert(second_plan);
    const std::uint32_t overlap_first = one_row_down.value().first_index;
    const std::uint32_t overlap_end =
        top.value().end_index() < one_row_down.value().end_index()
            ? top.value().end_index()
            : one_row_down.value().end_index();
    for (std::uint32_t item = overlap_first; item < overlap_end; ++item) {
        assert(retained_item(second_plan.value(), item));
        assert(slot_for(first_plan.value(), item) == slot_for(second_plan.value(), item));
    }
    assert(recycler.active_count() == one_row_down.value().count);

    recycler.reset();
    assert(recycler.active_count() == 0U);

    // Stable keys preserve the materialized semantic slot when a logical item
    // moves to a new index after insertion/reordering.
    auto keyed_window = os::ui::plan_collection_window(os::ui::CollectionWindowRequest{
        .item_count = 4U,
        .item_extent_q6 = os::ui::logical_from_dp(56U),
        .viewport_extent_q6 = os::ui::logical_from_dp(224U),
        .overscan_items = 0U,
    });
    assert(keyed_window);
    assert(keyed_window.value().count == 4U);

    const os::ui::CollectionItemKey key_a{101U};
    const os::ui::CollectionItemKey key_b{102U};
    const os::ui::CollectionItemKey key_c{103U};
    const os::ui::CollectionItemKey key_d{104U};
    const os::ui::CollectionItemKey key_new{999U};

    os::ui::CollectionRecycleRequest keyed_first{};
    keyed_first.window = keyed_window.value();
    keyed_first.key_count = 4U;
    keyed_first.item_keys[0] = key_a;
    keyed_first.item_keys[1] = key_b;
    keyed_first.item_keys[2] = key_c;
    keyed_first.item_keys[3] = key_d;
    auto keyed_first_plan = recycler.bind(keyed_first);
    assert(keyed_first_plan);

    os::ui::CollectionRecycleRequest keyed_after_insert{};
    keyed_after_insert.window = keyed_window.value();
    keyed_after_insert.key_count = 4U;
    keyed_after_insert.item_keys[0] = key_new;
    keyed_after_insert.item_keys[1] = key_a;
    keyed_after_insert.item_keys[2] = key_b;
    keyed_after_insert.item_keys[3] = key_c;
    auto keyed_second_plan = recycler.bind(keyed_after_insert);
    assert(keyed_second_plan);
    assert(retained_key(keyed_second_plan.value(), key_a));
    assert(retained_key(keyed_second_plan.value(), key_b));
    assert(retained_key(keyed_second_plan.value(), key_c));
    assert(!retained_key(keyed_second_plan.value(), key_new));
    assert(slot_for_key(keyed_first_plan.value(), key_a) ==
        slot_for_key(keyed_second_plan.value(), key_a));
    assert(slot_for_key(keyed_first_plan.value(), key_b) ==
        slot_for_key(keyed_second_plan.value(), key_b));
    assert(slot_for_key(keyed_first_plan.value(), key_c) ==
        slot_for_key(keyed_second_plan.value(), key_c));
    assert(keyed_second_plan.value().bindings[1].item_index == 1U);

    auto duplicate_keys = keyed_after_insert;
    duplicate_keys.item_keys[3] = key_b;
    auto duplicate_plan = recycler.bind(duplicate_keys);
    assert(!duplicate_plan);
    expect_ui_error(duplicate_plan.error(), os::ui::errors::invalid_collection);

    auto zero_key = keyed_after_insert;
    zero_key.item_keys[0] = {};
    auto zero_key_plan = recycler.bind(zero_key);
    assert(!zero_key_plan);
    expect_ui_error(zero_key_plan.error(), os::ui::errors::invalid_collection);

    recycler.reset();
    assert(recycler.active_count() == 0U);

    // A data-source materialization is revision-scoped. All key lookups must
    // resolve against one captured logical collection state.
    TestCollectionSource source{};
    source.revision = 10U;
    source.item_count = 4U;
    source.keys[0] = key_a;
    source.keys[1] = key_b;
    source.keys[2] = key_c;
    source.keys[3] = key_d;
    const os::ui::CollectionDataSourceBackend backend{
        .context = &source,
        .snapshot = snapshot_source,
        .item_key_at = source_key_at,
    };

    auto source_snapshot = os::ui::collection_data_snapshot(backend);
    assert(source_snapshot);
    assert(source_snapshot.value().revision == os::ui::CollectionRevision{10U});
    assert(source_snapshot.value().item_count == 4U);

    auto source_window = os::ui::plan_collection_window(os::ui::CollectionWindowRequest{
        .item_count = source_snapshot.value().item_count,
        .item_extent_q6 = os::ui::logical_from_dp(56U),
        .viewport_extent_q6 = os::ui::logical_from_dp(224U),
        .overscan_items = 0U,
    });
    assert(source_window);
    auto source_request = os::ui::build_collection_recycle_request(
        source_window.value(), source_snapshot.value(), backend);
    assert(source_request);
    auto source_first_plan = recycler.bind(source_request.value());
    assert(source_first_plan);

    // Insert a new logical item at the front and advance the source revision.
    // Keys A/B/C move indices but retain their semantic recycler slots.
    source.revision = 11U;
    source.item_count = 5U;
    source.keys[0] = key_new;
    source.keys[1] = key_a;
    source.keys[2] = key_b;
    source.keys[3] = key_c;
    source.keys[4] = key_d;

    auto source_snapshot2 = os::ui::collection_data_snapshot(backend);
    assert(source_snapshot2);
    auto source_window2 = os::ui::plan_collection_window(os::ui::CollectionWindowRequest{
        .item_count = source_snapshot2.value().item_count,
        .item_extent_q6 = os::ui::logical_from_dp(56U),
        .viewport_extent_q6 = os::ui::logical_from_dp(224U),
        .overscan_items = 0U,
    });
    assert(source_window2);
    auto source_request2 = os::ui::build_collection_recycle_request(
        source_window2.value(), source_snapshot2.value(), backend);
    assert(source_request2);
    auto source_second_plan = recycler.bind(source_request2.value());
    assert(source_second_plan);
    assert(retained_key(source_second_plan.value(), key_a));
    assert(retained_key(source_second_plan.value(), key_b));
    assert(retained_key(source_second_plan.value(), key_c));
    assert(slot_for_key(source_first_plan.value(), key_a) ==
        slot_for_key(source_second_plan.value(), key_a));
    assert(slot_for_key(source_first_plan.value(), key_b) ==
        slot_for_key(source_second_plan.value(), key_b));
    assert(slot_for_key(source_first_plan.value(), key_c) ==
        slot_for_key(source_second_plan.value(), key_c));

    // Reusing the old revision after mutation must fail instead of mixing keys
    // from two logical states into one materialized semantic window.
    auto stale_request = os::ui::build_collection_recycle_request(
        source_window.value(), source_snapshot.value(), backend);
    assert(!stale_request);
    expect_ui_error(stale_request.error(), os::ui::errors::stale_collection_snapshot);

    // A source returning duplicate or zero stable keys is malformed even if
    // the callbacks themselves return success.
    source.revision = 12U;
    source.item_count = 4U;
    source.keys[0] = key_a;
    source.keys[1] = key_b;
    source.keys[2] = key_b;
    source.keys[3] = key_d;
    auto malformed_snapshot = os::ui::collection_data_snapshot(backend);
    assert(malformed_snapshot);
    auto malformed_request = os::ui::build_collection_recycle_request(
        keyed_window.value(), malformed_snapshot.value(), backend);
    assert(!malformed_request);
    expect_ui_error(malformed_request.error(), os::ui::errors::invalid_collection_source);

    source.revision = 13U;
    source.keys[2] = {};
    auto zero_source_snapshot = os::ui::collection_data_snapshot(backend);
    assert(zero_source_snapshot);
    auto zero_source_request = os::ui::build_collection_recycle_request(
        keyed_window.value(), zero_source_snapshot.value(), backend);
    assert(!zero_source_request);
    expect_ui_error(zero_source_request.error(), os::ui::errors::invalid_collection_source);

    auto missing_backend_snapshot = os::ui::collection_data_snapshot(
        os::ui::CollectionDataSourceBackend{});
    assert(!missing_backend_snapshot);
    expect_ui_error(
        missing_backend_snapshot.error(),
        os::ui::errors::invalid_collection_source);

    recycler.reset();
    assert(recycler.active_count() == 0U);

    auto empty = os::ui::plan_collection_window(os::ui::CollectionWindowRequest{
        .item_count = 0U,
        .viewport_extent_q6 = os::ui::logical_from_dp(640U),
    });
    assert(empty);
    assert(empty.value().count == 0U);
    assert(empty.value().content_extent_q6 == 0U);
    auto empty_plan = recycler.bind(empty.value());
    assert(empty_plan);
    assert(empty_plan.value().count == 0U);

    auto too_dense = os::ui::plan_collection_window(os::ui::CollectionWindowRequest{
        .item_count = 10'000U,
        .item_extent_q6 = os::ui::logical_from_dp(8U),
        .viewport_extent_q6 = os::ui::logical_from_dp(640U),
        .overscan_items = 0U,
    });
    assert(!too_dense);
    expect_ui_error(too_dense.error(), os::ui::errors::collection_window_limit);

    auto overscrolled = os::ui::plan_collection_window(os::ui::CollectionWindowRequest{
        .item_count = 100U,
        .item_extent_q6 = os::ui::logical_from_dp(56U),
        .scroll_offset_q6 = static_cast<std::uint64_t>(100U) * os::ui::logical_from_dp(56U),
        .viewport_extent_q6 = os::ui::logical_from_dp(640U),
    });
    assert(!overscrolled);
    expect_ui_error(overscrolled.error(), os::ui::errors::invalid_collection);

    auto too_many = os::ui::plan_collection_window(os::ui::CollectionWindowRequest{
        .item_count = os::ui::max_collection_items + 1U,
        .item_extent_q6 = os::ui::logical_from_dp(56U),
        .viewport_extent_q6 = os::ui::logical_from_dp(640U),
    });
    assert(!too_many);
    expect_ui_error(too_many.error(), os::ui::errors::invalid_collection);

    return 0;
}
