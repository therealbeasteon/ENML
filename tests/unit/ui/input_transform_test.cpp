#include <cassert>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/ui/error.hpp>
#include <os/ui/input.hpp>

namespace {

void expect_ui_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::ui);
    assert(error.code == code);
}

} // namespace

int main() {
    const os::ui::InputViewportTransform phone{
        .surface_width_px = 1080U,
        .surface_height_px = 2400U,
        .logical_width_q6 = os::ui::logical_from_dp(360U),
        .logical_height_q6 = os::ui::logical_from_dp(800U),
    };

    auto center = os::ui::logical_point_from_surface_pixel(
        phone,
        {.x = 540U, .y = 1200U});
    assert(center);
    assert(center.value().x_q6 == static_cast<std::int32_t>(os::ui::logical_from_dp(180U)));
    assert(center.value().y_q6 == static_cast<std::int32_t>(os::ui::logical_from_dp(400U)));

    auto origin = os::ui::logical_point_from_surface_pixel(phone, {.x = 0U, .y = 0U});
    assert(origin);
    assert(origin.value().x_q6 == 0);
    assert(origin.value().y_q6 == 0);

    auto last = os::ui::logical_point_from_surface_pixel(
        phone,
        {.x = 1079U, .y = 2399U});
    assert(last);
    assert(last.value().x_q6 >= 0);
    assert(last.value().y_q6 >= 0);
    assert(
        static_cast<std::uint32_t>(last.value().x_q6) <
        os::ui::logical_from_dp(360U));
    assert(
        static_cast<std::uint32_t>(last.value().y_q6) <
        os::ui::logical_from_dp(800U));

    // The physical surface edge is half-open. Out-of-surface input fails
    // instead of being clamped into a control at the final logical pixel.
    auto edge = os::ui::logical_point_from_surface_pixel(
        phone,
        {.x = 1080U, .y = 1200U});
    assert(!edge);
    expect_ui_error(edge.error(), os::ui::errors::invalid_input_point);

    auto invalid_transform = os::ui::logical_point_from_surface_pixel(
        os::ui::InputViewportTransform{
            .surface_width_px = 0U,
            .surface_height_px = 2400U,
            .logical_width_q6 = os::ui::logical_from_dp(360U),
            .logical_height_q6 = os::ui::logical_from_dp(800U),
        },
        {.x = 0U, .y = 0U});
    assert(!invalid_transform);
    expect_ui_error(
        invalid_transform.error(),
        os::ui::errors::invalid_input_transform);

    // Non-integer scale ratios remain deterministic and need no float state.
    const os::ui::InputViewportTransform modern_phone{
        .surface_width_px = 1080U,
        .surface_height_px = 2400U,
        .logical_width_q6 = os::ui::logical_from_dp(390U),
        .logical_height_q6 = os::ui::logical_from_dp(844U),
    };
    auto modern_center = os::ui::logical_point_from_surface_pixel(
        modern_phone,
        {.x = 540U, .y = 1200U});
    assert(modern_center);
    assert(
        modern_center.value().x_q6 ==
        static_cast<std::int32_t>(os::ui::logical_from_dp(195U)));
    assert(
        modern_center.value().y_q6 ==
        static_cast<std::int32_t>(os::ui::logical_from_dp(422U)));

    return 0;
}
