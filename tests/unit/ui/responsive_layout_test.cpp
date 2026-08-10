#include <os/ui/types.hpp>

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
    const os::ui::LogicalViewport phone{
        .width_q6 = os::ui::logical_from_dp(360U),
        .height_q6 = os::ui::logical_from_dp(800U),
        .safe_insets = {
            .top_q6 = os::ui::logical_from_dp(24U),
            .right_q6 = 0U,
            .bottom_q6 = os::ui::logical_from_dp(24U),
            .left_q6 = 0U,
        },
    };
    auto phone_layout = os::ui::layout_list_detail(phone);
    assert(phone_layout);
    assert(phone_layout.value().mode == os::ui::PaneLayoutMode::single);
    assert(phone_layout.value().primary.x_q6 == 0);
    assert(phone_layout.value().primary.y_q6 ==
        static_cast<std::int32_t>(os::ui::logical_from_dp(24U)));
    assert(phone_layout.value().primary.width_q6 == os::ui::logical_from_dp(360U));
    assert(phone_layout.value().primary.height_q6 == os::ui::logical_from_dp(752U));
    assert(!phone_layout.value().secondary.nonempty());

    const os::ui::LogicalViewport tablet{
        .width_q6 = os::ui::logical_from_dp(800U),
        .height_q6 = os::ui::logical_from_dp(600U),
        .safe_insets = {
            .top_q6 = os::ui::logical_from_dp(20U),
            .right_q6 = os::ui::logical_from_dp(20U),
            .bottom_q6 = os::ui::logical_from_dp(20U),
            .left_q6 = os::ui::logical_from_dp(20U),
        },
    };
    auto tablet_layout = os::ui::layout_list_detail(tablet);
    assert(tablet_layout);
    assert(tablet_layout.value().mode == os::ui::PaneLayoutMode::dual);
    assert(tablet_layout.value().primary.x_q6 ==
        static_cast<std::int32_t>(os::ui::logical_from_dp(20U)));
    assert(tablet_layout.value().primary.y_q6 ==
        static_cast<std::int32_t>(os::ui::logical_from_dp(20U)));
    assert(tablet_layout.value().primary.width_q6 >= os::ui::logical_from_dp(220U));
    assert(tablet_layout.value().primary.width_q6 <= os::ui::logical_from_dp(360U));
    assert(tablet_layout.value().secondary.width_q6 >= os::ui::logical_from_dp(280U));
    assert(tablet_layout.value().secondary.x_q6 ==
        tablet_layout.value().primary.x_q6 +
        static_cast<std::int32_t>(tablet_layout.value().primary.width_q6) +
        static_cast<std::int32_t>(os::ui::logical_from_dp(16U)));
    const auto safe_width = os::ui::logical_from_dp(760U);
    assert(tablet_layout.value().primary.width_q6 +
        os::ui::logical_from_dp(16U) +
        tablet_layout.value().secondary.width_q6 == safe_width);

    os::ui::ResponsivePolicy wider_breakpoint{};
    wider_breakpoint.two_pane_min_width_q6 = os::ui::logical_from_dp(900U);
    auto forced_single = os::ui::layout_list_detail(tablet, wider_breakpoint);
    assert(forced_single);
    assert(forced_single.value().mode == os::ui::PaneLayoutMode::single);

    os::ui::ResponsivePolicy insufficient{};
    insufficient.two_pane_min_width_q6 = os::ui::logical_from_dp(400U);
    insufficient.primary_min_width_q6 = os::ui::logical_from_dp(300U);
    insufficient.primary_max_width_q6 = os::ui::logical_from_dp(320U);
    insufficient.secondary_min_width_q6 = os::ui::logical_from_dp(300U);
    insufficient.pane_gap_q6 = os::ui::logical_from_dp(32U);
    const os::ui::LogicalViewport narrow{
        .width_q6 = os::ui::logical_from_dp(600U),
        .height_q6 = os::ui::logical_from_dp(500U),
    };
    auto fallback = os::ui::layout_list_detail(narrow, insufficient);
    assert(fallback);
    assert(fallback.value().mode == os::ui::PaneLayoutMode::single);

    const os::ui::LogicalViewport bad_insets{
        .width_q6 = os::ui::logical_from_dp(320U),
        .height_q6 = os::ui::logical_from_dp(480U),
        .safe_insets = {
            .right_q6 = os::ui::logical_from_dp(200U),
            .left_q6 = os::ui::logical_from_dp(200U),
        },
    };
    auto invalid_viewport = os::ui::layout_list_detail(bad_insets);
    assert(!invalid_viewport);
    expect_ui_error(invalid_viewport.error(), os::ui::errors::invalid_viewport);

    os::ui::ResponsivePolicy bad_policy{};
    bad_policy.primary_share_denominator = 0U;
    auto invalid_policy = os::ui::layout_list_detail(tablet, bad_policy);
    assert(!invalid_policy);
    expect_ui_error(invalid_policy.error(), os::ui::errors::invalid_viewport);

    assert(os::ui::logical_from_dp(1U) == 64U);
    assert(os::ui::logical_from_dp(os::ui::max_logical_dimension_dp + 1U) == 0U);

    return 0;
}
