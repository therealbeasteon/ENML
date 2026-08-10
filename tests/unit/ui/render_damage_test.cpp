#include <cassert>
#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/ui/error.hpp>
#include <os/ui/render_damage.hpp>

namespace {

[[nodiscard]] os::ui::LogicalRect rect(
    std::uint32_t x,
    std::uint32_t y,
    std::uint32_t width,
    std::uint32_t height) {
    return os::ui::LogicalRect{
        .x_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(x)),
        .y_q6 = static_cast<std::int32_t>(os::ui::logical_from_dp(y)),
        .width_q6 = os::ui::logical_from_dp(width),
        .height_q6 = os::ui::logical_from_dp(height),
    };
}

void expect_ui_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::ui);
    assert(error.code == code);
}

os::ui::RenderCommand command(os::ui::UiNodeId id, os::ui::LogicalRect bounds) {
    os::ui::RenderCommand output{};
    output.source = id;
    output.bounds = bounds;
    return output;
}

} // namespace

int main() {
    os::ui::RenderCommandBuffer previous{};
    previous.revision = 10U;
    previous.count = 3U;
    previous.commands[0] = command(os::ui::UiNodeId{1U}, rect(0U, 0U, 360U, 800U));
    previous.commands[1] = command(os::ui::UiNodeId{2U}, rect(20U, 100U, 140U, 48U));
    previous.commands[2] = command(os::ui::UiNodeId{3U}, rect(20U, 180U, 140U, 48U));

    auto next = previous;
    next.revision = 11U;
    next.commands[1].bounds = rect(40U, 120U, 140U, 48U);

    os::ui::RendererDelta moved{};
    moved.revision = 11U;
    moved.changed[0] = os::ui::UiNodeId{2U};
    moved.changed_count = 1U;

    auto moved_plan = os::ui::plan_render_damage(previous, next, moved);
    assert(moved_plan);
    assert(!moved_plan.value().full_redraw);
    assert(moved_plan.value().count == 2U);
    assert(moved_plan.value().rects[0] == rect(20U, 100U, 140U, 48U));
    assert(moved_plan.value().rects[1] == rect(40U, 120U, 140U, 48U));

    // A style/text/state change that keeps geometry damages the node once, not
    // two duplicate rectangles for old/new identical bounds.
    auto same_geometry = previous;
    same_geometry.revision = 11U;
    os::ui::RendererDelta changed{};
    changed.revision = 11U;
    changed.changed[0] = os::ui::UiNodeId{3U};
    changed.changed_count = 1U;
    auto changed_plan = os::ui::plan_render_damage(previous, same_geometry, changed);
    assert(changed_plan);
    assert(!changed_plan.value().full_redraw);
    assert(changed_plan.value().count == 1U);
    assert(changed_plan.value().rects[0] == previous.commands[2].bounds);

    // A renderable node that becomes hidden/unstyled disappears from the next
    // command buffer, so its old pixels must be restored.
    os::ui::RenderCommandBuffer hidden{};
    hidden.revision = 11U;
    hidden.count = 2U;
    hidden.commands[0] = previous.commands[0];
    hidden.commands[1] = previous.commands[2];
    os::ui::RendererDelta hidden_delta{};
    hidden_delta.revision = 11U;
    hidden_delta.changed[0] = os::ui::UiNodeId{2U};
    hidden_delta.changed_count = 1U;
    auto hidden_plan = os::ui::plan_render_damage(previous, hidden, hidden_delta);
    assert(hidden_plan);
    assert(hidden_plan.value().count == 1U);
    assert(hidden_plan.value().rects[0] == previous.commands[1].bounds);

    // A semantic-only node can be dirty without appearing in either render
    // command buffer; accessibility/input work should not force pixel work.
    os::ui::RendererDelta semantic_only{};
    semantic_only.revision = 11U;
    semantic_only.changed[0] = os::ui::UiNodeId{99U};
    semantic_only.changed_count = 1U;
    auto semantic_plan = os::ui::plan_render_damage(previous, same_geometry, semantic_only);
    assert(semantic_plan);
    assert(!semantic_plan.value().full_redraw);
    assert(semantic_plan.value().count == 0U);

    // Removed nodes use old geometry and must not still exist in the next
    // command buffer.
    os::ui::RenderCommandBuffer removed{};
    removed.revision = 11U;
    removed.count = 2U;
    removed.commands[0] = previous.commands[0];
    removed.commands[1] = previous.commands[1];
    os::ui::RendererDelta removed_delta{};
    removed_delta.revision = 11U;
    removed_delta.removed[0] = os::ui::UiNodeId{3U};
    removed_delta.removed_count = 1U;
    auto removed_plan = os::ui::plan_render_damage(previous, removed, removed_delta);
    assert(removed_plan);
    assert(removed_plan.value().count == 1U);
    assert(removed_plan.value().rects[0] == previous.commands[2].bounds);

    auto inconsistent_removed = os::ui::plan_render_damage(previous, same_geometry, removed_delta);
    assert(!inconsistent_removed);
    expect_ui_error(
        inconsistent_removed.error(),
        os::ui::errors::invalid_render_damage);

    // Full-resync metadata and an initial frame both explicitly fall back to a
    // full redraw rather than manufacturing a huge region array.
    os::ui::RendererDelta resync{};
    resync.revision = 11U;
    resync.full_resync_required = true;
    auto resync_plan = os::ui::plan_render_damage(previous, same_geometry, resync);
    assert(resync_plan);
    assert(resync_plan.value().full_redraw);
    assert(resync_plan.value().count == 0U);

    os::ui::RenderCommandBuffer empty_previous{};
    auto initial = os::ui::plan_render_damage(empty_previous, same_geometry, resync);
    assert(initial);
    assert(initial.value().full_redraw);

    // More distinct dirty rectangles than the fixed budget degrades to full
    // redraw. No heap region list appears just because a frame is pathological.
    os::ui::RenderCommandBuffer many_previous{};
    os::ui::RenderCommandBuffer many_next{};
    many_previous.revision = 20U;
    many_next.revision = 21U;
    many_previous.count = os::ui::max_render_damage_rects + 1U;
    many_next.count = many_previous.count;
    os::ui::RendererDelta many_delta{};
    many_delta.revision = 21U;
    many_delta.changed_count = many_previous.count;
    for (std::size_t index = 0U; index < many_previous.count; ++index) {
        const auto id = os::ui::UiNodeId{static_cast<std::uint32_t>(index + 1U)};
        const auto bounds = rect(
            static_cast<std::uint32_t>(index * 2U),
            static_cast<std::uint32_t>(index * 2U),
            1U,
            1U);
        many_previous.commands[index] = command(id, bounds);
        many_next.commands[index] = command(id, bounds);
        many_delta.changed[index] = id;
    }
    auto many_plan = os::ui::plan_render_damage(many_previous, many_next, many_delta);
    assert(many_plan);
    assert(many_plan.value().full_redraw);
    assert(many_plan.value().count == 0U);

    auto bad_revision = changed;
    bad_revision.revision = 12U;
    auto invalid = os::ui::plan_render_damage(previous, same_geometry, bad_revision);
    assert(!invalid);
    expect_ui_error(invalid.error(), os::ui::errors::invalid_render_damage);

    return 0;
}
