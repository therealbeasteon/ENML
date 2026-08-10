#include <os/ui/collection.hpp>
#include <os/ui/contour.hpp>
#include <os/ui/design.hpp>
#include <os/ui/tree.hpp>
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

[[nodiscard]] os::ui::LogicalRect from_pane(const os::ui::LogicalRect& pane) {
    return pane;
}

} // namespace

int main() {
    auto primary = os::ui::resolve_style_token(os::ui::style_tokens::primary_action);
    assert(primary);
    assert(primary.value().foreground == os::ui::ColorRole::on_accent);
    assert(primary.value().background == os::ui::ColorRole::accent);
    assert(primary.value().typography == os::ui::TypographyRole::label);
    assert(primary.value().material == os::ui::OpticalMaterialRole::opaque);
    assert(primary.value().depth == os::ui::DepthRole::raised);
    assert(primary.value().curve == os::ui::CurveRole::continuous);
    assert(primary.value().motion == os::ui::MotionRole::responsive);

    assert(os::ui::style_token_valid(os::ui::StyleTokenId{}));
    assert(os::ui::style_token_valid(os::ui::style_tokens::translucent_panel));
    assert(os::ui::style_token_valid(os::ui::style_tokens::hero_surface));
    assert(!os::ui::style_token_valid(os::ui::StyleTokenId{500U}));

    auto optical = os::ui::resolve_visual_style(os::ui::style_tokens::translucent_panel);
    assert(optical);
    assert(optical.value().token.material == os::ui::OpticalMaterialRole::crystal);
    assert(optical.value().material.opacity_percent < 100U);
    assert(optical.value().material.backdrop_blur_q6 > 0U);
    assert(optical.value().material.live_backdrop_allowed);
    assert(optical.value().curve.asymmetric_contour_allowed);
    assert(optical.value().curve.smoothing_percent > 50U);
    assert(optical.value().motion.duration_ms > 0U);
    assert(optical.value().motion.spatial_motion_allowed);

    auto reduced = os::ui::resolve_visual_style(
        os::ui::style_tokens::translucent_panel,
        os::ui::VisualPreferences{
            .reduce_transparency = true,
            .reduce_motion = true,
        });
    assert(reduced);
    assert(reduced.value().material.opacity_percent == 100U);
    assert(reduced.value().material.backdrop_blur_q6 == 0U);
    assert(!reduced.value().material.live_backdrop_allowed);
    assert(reduced.value().motion.duration_ms == 80U);
    assert(reduced.value().motion.curve == os::ui::MotionCurve::ease_out);
    assert(!reduced.value().motion.spatial_motion_allowed);

    auto contrast = os::ui::resolve_visual_style(
        os::ui::style_tokens::hero_surface,
        os::ui::VisualPreferences{.high_contrast = true});
    assert(contrast);
    assert(contrast.value().material.opacity_percent == 100U);
    assert(contrast.value().token.material_tint == os::ui::ColorRole::transparent);

    auto hero_depth = os::ui::depth_metrics(os::ui::DepthRole::hero);
    auto floating_depth = os::ui::depth_metrics(os::ui::DepthRole::floating);
    assert(hero_depth && floating_depth);
    assert(hero_depth.value().offset_q6 > floating_depth.value().offset_q6);
    assert(hero_depth.value().blur_q6 > floating_depth.value().blur_q6);

    auto invalid_material = os::ui::material_metrics(
        static_cast<os::ui::OpticalMaterialRole>(255U));
    assert(!invalid_material);
    expect_ui_error(invalid_material.error(), os::ui::errors::invalid_style);

    const os::ui::LogicalRect contour_bounds{
        .x_q6 = 0,
        .y_q6 = 0,
        .width_q6 = os::ui::logical_from_dp(320U),
        .height_q6 = os::ui::logical_from_dp(80U),
    };
    auto swept = os::ui::resolve_contour(contour_bounds, os::ui::CurveRole::swept);
    assert(swept);
    assert(swept.value().asymmetric);
    assert(swept.value().smoothing_percent > 50U);
    assert(swept.value().radii.top_left_q6 < swept.value().radii.top_right_q6);
    assert(swept.value().radii.bottom_left_q6 > swept.value().radii.bottom_right_q6);

    const os::ui::LogicalRect capsule_bounds{
        .x_q6 = 0,
        .y_q6 = 0,
        .width_q6 = os::ui::logical_from_dp(200U),
        .height_q6 = os::ui::logical_from_dp(48U),
    };
    auto capsule = os::ui::resolve_contour(capsule_bounds, os::ui::CurveRole::capsule);
    assert(capsule);
    const auto capsule_radius = os::ui::logical_from_dp(24U);
    assert(capsule.value().radii.top_left_q6 == capsule_radius);
    assert(capsule.value().radii.top_right_q6 == capsule_radius);
    assert(capsule.value().radii.bottom_right_q6 == capsule_radius);
    assert(capsule.value().radii.bottom_left_q6 == capsule_radius);

    auto invalid_contour = os::ui::resolve_contour(
        os::ui::LogicalRect{},
        os::ui::CurveRole::continuous);
    assert(!invalid_contour);
    expect_ui_error(invalid_contour.error(), os::ui::errors::invalid_bounds);

    auto normal = os::ui::typography_metrics(os::ui::TypographyRole::body, 100U);
    auto large = os::ui::typography_metrics(os::ui::TypographyRole::body, 200U);
    auto maximum = os::ui::typography_metrics(os::ui::TypographyRole::body, 300U);
    assert(normal && large && maximum);
    assert(large.value().size_q6 == normal.value().size_q6 * 2U);
    assert(large.value().line_height_q6 == normal.value().line_height_q6 * 2U);
    assert(maximum.value().line_height_q6 == normal.value().line_height_q6 * 3U);

    auto invalid_scale = os::ui::typography_metrics(os::ui::TypographyRole::body, 301U);
    assert(!invalid_scale);
    expect_ui_error(invalid_scale.error(), os::ui::errors::invalid_text_scale);

    auto vertical_padding = os::ui::spacing_q6(os::ui::SpacingRole::sm);
    assert(vertical_padding);
    const std::uint32_t normal_row =
        normal.value().line_height_q6 + vertical_padding.value() * 2U >
                os::ui::minimum_touch_target_q6
            ? normal.value().line_height_q6 + vertical_padding.value() * 2U
            : os::ui::minimum_touch_target_q6;
    const std::uint32_t large_row =
        large.value().line_height_q6 + vertical_padding.value() * 2U >
                os::ui::minimum_touch_target_q6
            ? large.value().line_height_q6 + vertical_padding.value() * 2U
            : os::ui::minimum_touch_target_q6;
    assert(large_row > normal_row);

    auto normal_window = os::ui::plan_collection_window(os::ui::CollectionWindowRequest{
        .item_count = 2'000U,
        .item_extent_q6 = normal_row,
        .viewport_extent_q6 = os::ui::logical_from_dp(600U),
        .overscan_items = 2U,
    });
    auto large_window = os::ui::plan_collection_window(os::ui::CollectionWindowRequest{
        .item_count = 2'000U,
        .item_extent_q6 = large_row,
        .viewport_extent_q6 = os::ui::logical_from_dp(600U),
        .overscan_items = 2U,
    });
    assert(normal_window && large_window);
    assert(large_window.value().count < normal_window.value().count);

    const os::ui::LogicalViewport phone{
        .width_q6 = os::ui::logical_from_dp(360U),
        .height_q6 = os::ui::logical_from_dp(800U),
    };
    const os::ui::LogicalViewport tablet{
        .width_q6 = os::ui::logical_from_dp(840U),
        .height_q6 = os::ui::logical_from_dp(700U),
    };
    auto phone_layout = os::ui::layout_list_detail(phone);
    auto tablet_layout = os::ui::layout_list_detail(tablet);
    assert(phone_layout && tablet_layout);
    assert(phone_layout.value().mode == os::ui::PaneLayoutMode::single);
    assert(tablet_layout.value().mode == os::ui::PaneLayoutMode::dual);

    os::ui::SemanticTree tree{from_pane(phone_layout.value().primary)};
    assert(tree.valid());
    auto list = tree.add(
        tree.root(),
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::list,
            .bounds = from_pane(phone_layout.value().primary),
            .style = os::ui::style_tokens::surface,
        });
    assert(list);
    const auto stable_list_id = list.value().id;

    // Recompose geometry for a wider viewport without replacing semantic
    // identity. Large-text layout changes row extent, not the list's UiNodeId.
    assert(tree.set_bounds(tree.root(), from_pane(tablet_layout.value().primary)));
    assert(tree.set_bounds(stable_list_id, from_pane(tablet_layout.value().primary)));
    auto recomposed = tree.lookup(stable_list_id);
    assert(recomposed);
    assert(recomposed.value().id == stable_list_id);
    assert(recomposed.value().spec.bounds == tablet_layout.value().primary);

    return 0;
}
