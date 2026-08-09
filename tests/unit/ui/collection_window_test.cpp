#include <os/ui/collection.hpp>

#include <cassert>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/ui/error.hpp>

namespace {

void expect_ui_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::ui);
    assert(error.code == code);
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

    auto empty = os::ui::plan_collection_window(os::ui::CollectionWindowRequest{
        .item_count = 0U,
        .viewport_extent_q6 = os::ui::logical_from_dp(640U),
    });
    assert(empty);
    assert(empty.value().count == 0U);
    assert(empty.value().content_extent_q6 == 0U);

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
