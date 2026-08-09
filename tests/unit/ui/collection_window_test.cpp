#include <os/ui/collection.hpp>

#include <array>
#include <cassert>
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
