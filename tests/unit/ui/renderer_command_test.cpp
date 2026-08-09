#include <os/ui/renderer.hpp>
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

[[nodiscard]] const os::ui::RenderCommand* find_command(
    const os::ui::RenderCommandBuffer& buffer,
    os::ui::UiNodeId id) {
    for (std::size_t index = 0U; index < buffer.count; ++index) {
        if (buffer.commands[index].source == id) return &buffer.commands[index];
    }
    return nullptr;
}

} // namespace

int main() {
    const os::ui::LogicalRect root_bounds{
        .x_q6 = 0,
        .y_q6 = 0,
        .width_q6 = os::ui::logical_from_dp(390U),
        .height_q6 = os::ui::logical_from_dp(844U),
    };
    os::ui::SemanticTree tree{root_bounds};
    assert(tree.valid());

    auto panel = tree.add(
        tree.root(),
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::container,
            .bounds = os::ui::LogicalRect{
                .x_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(20U)),
                .y_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(80U)),
                .width_q6 = os::ui::logical_from_dp(350U),
                .height_q6 = os::ui::logical_from_dp(420U),
            },
            .style = os::ui::style_tokens::translucent_panel,
        });
    assert(panel);

    auto title_text = os::ui::make_semantic_text("A crafted ENML surface");
    assert(title_text);
    auto title = tree.add(
        panel.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::text,
            .bounds = os::ui::LogicalRect{
                .x_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(44U)),
                .y_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(112U)),
                .width_q6 = os::ui::logical_from_dp(280U),
                .height_q6 = os::ui::logical_from_dp(64U),
            },
            .style = os::ui::style_tokens::title_text,
            .label = title_text.value(),
        });
    assert(title);

    auto button_label = os::ui::make_semantic_text("Continue");
    assert(button_label);
    auto button = tree.add(
        panel.value().id,
        os::ui::UiNodeSpec{
            .role = os::ui::UiRole::button,
            .bounds = os::ui::LogicalRect{
                .x_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(44U)),
                .y_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(400U)),
                .width_q6 = os::ui::logical_from_dp(180U),
                .height_q6 = os::ui::logical_from_dp(56U),
            },
            .actions = os::ui::action_mask(os::ui::UiAction::activate) |
                os::ui::action_mask(os::ui::UiAction::focus),
            .style = os::ui::style_tokens::primary_action,
            .label = button_label.value(),
        });
    assert(button);
    assert(tree.focus(button.value().id));

    const auto snapshot = tree.renderer_snapshot();
    auto full = os::ui::build_render_commands(
        snapshot,
        os::ui::RenderBuildOptions{
            .preferences = {},
            .text_scale_percent = 200U,
            .quality = os::ui::VisualQualityTier::full,
        });
    assert(full);
    assert(full.value().revision == snapshot.revision);
    assert(full.value().count == 3U); // unstyled semantic root emits no paint intent

    const auto* panel_command = find_command(full.value(), panel.value().id);
    const auto* title_command = find_command(full.value(), title.value().id);
    const auto* button_command = find_command(full.value(), button.value().id);
    assert(panel_command != nullptr);
    assert(title_command != nullptr);
    assert(button_command != nullptr);

    assert(panel_command->visual.token.material == os::ui::OpticalMaterialRole::crystal);
    assert(panel_command->visual.material.live_backdrop_allowed);
    assert(panel_command->contour.role == os::ui::CurveRole::swept);
    assert(panel_command->contour.asymmetric);
    assert(panel_command->visual.depth.offset_q6 > 0U);

    assert(title_command->content == os::ui::RenderContentKind::text);
    assert(title_command->visual_text == title_text.value());
    assert(title_command->typography.size_q6 == os::ui::logical_from_dp(40U));

    assert(button_command->content == os::ui::RenderContentKind::control);
    assert(button_command->visual_text.empty());
    assert(button_command->focus_visible);
    assert(button_command->visual.motion.duration_ms > 0U);

    auto economy = os::ui::build_render_commands(
        snapshot,
        os::ui::RenderBuildOptions{
            .preferences = {},
            .text_scale_percent = 100U,
            .quality = os::ui::VisualQualityTier::economy,
        });
    assert(economy);
    const auto* economy_panel = find_command(economy.value(), panel.value().id);
    assert(economy_panel != nullptr);
    assert(economy_panel->visual.token.material == os::ui::OpticalMaterialRole::crystal);
    assert(economy_panel->visual.material.backdrop_blur_q6 == 0U);
    assert(!economy_panel->visual.material.live_backdrop_allowed);
    assert(economy_panel->contour == panel_command->contour);

    auto reduced = os::ui::build_render_commands(
        snapshot,
        os::ui::RenderBuildOptions{
            .preferences = os::ui::VisualPreferences{
                .reduce_transparency = true,
                .reduce_motion = true,
                .high_contrast = false,
            },
            .text_scale_percent = 100U,
            .quality = os::ui::VisualQualityTier::full,
        });
    assert(reduced);
    const auto* reduced_panel = find_command(reduced.value(), panel.value().id);
    assert(reduced_panel != nullptr);
    assert(reduced_panel->visual.material.opacity_percent == 100U);
    assert(reduced_panel->visual.material.backdrop_blur_q6 == 0U);
    assert(!reduced_panel->visual.material.live_backdrop_allowed);
    assert(reduced_panel->visual.motion.duration_ms == 80U);
    assert(!reduced_panel->visual.motion.spatial_motion_allowed);

    os::ui::UiNodeState hidden_panel_state{};
    hidden_panel_state.visible = false;
    assert(tree.set_state(panel.value().id, hidden_panel_state));
    auto hidden = os::ui::build_render_commands(tree.renderer_snapshot());
    assert(hidden);
    assert(hidden.value().count == 0U);

    auto invalid_count = snapshot;
    invalid_count.count = os::ui::max_ui_nodes + 1U;
    auto invalid_snapshot = os::ui::build_render_commands(invalid_count);
    assert(!invalid_snapshot);
    expect_ui_error(invalid_snapshot.error(), os::ui::errors::invalid_render_snapshot);

    auto invalid_scale = os::ui::build_render_commands(
        snapshot,
        os::ui::RenderBuildOptions{
            .preferences = {},
            .text_scale_percent = 99U,
            .quality = os::ui::VisualQualityTier::balanced,
        });
    assert(!invalid_scale);
    expect_ui_error(invalid_scale.error(), os::ui::errors::invalid_text_scale);

    auto invalid_quality = os::ui::build_render_commands(
        snapshot,
        os::ui::RenderBuildOptions{
            .preferences = {},
            .text_scale_percent = 100U,
            .quality = static_cast<os::ui::VisualQualityTier>(0U),
        });
    assert(!invalid_quality);
    expect_ui_error(invalid_quality.error(), os::ui::errors::invalid_render_options);

    return 0;
}
