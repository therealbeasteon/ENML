#include <os/ui/render_damage.hpp>

#include <cstddef>
#include <cstdint>

#include <os/ui/error.hpp>

namespace os::ui {
namespace {

[[nodiscard]] bool command_buffer_valid(const RenderCommandBuffer& buffer) noexcept {
    if (buffer.count > buffer.commands.size()) return false;
    if (buffer.count != 0U && buffer.revision == 0U) return false;
    for (std::size_t index = 0U; index < buffer.count; ++index) {
        const auto& command = buffer.commands[index];
        if (command.source.value() == 0U || !command.bounds.bounded()) return false;
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (buffer.commands[earlier].source == command.source) return false;
        }
    }
    return true;
}

[[nodiscard]] bool delta_valid(const RendererDelta& delta) noexcept {
    if (delta.revision == 0U || delta.changed_count > delta.changed.size() ||
        delta.removed_count > delta.removed.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < delta.changed_count; ++index) {
        if (delta.changed[index].value() == 0U) return false;
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (delta.changed[earlier] == delta.changed[index]) return false;
        }
        for (std::size_t removed = 0U; removed < delta.removed_count; ++removed) {
            if (delta.changed[index] == delta.removed[removed]) return false;
        }
    }
    for (std::size_t index = 0U; index < delta.removed_count; ++index) {
        if (delta.removed[index].value() == 0U) return false;
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (delta.removed[earlier] == delta.removed[index]) return false;
        }
    }
    return true;
}

[[nodiscard]] const RenderCommand* find_command(
    const RenderCommandBuffer& buffer,
    UiNodeId source) noexcept {
    for (std::size_t index = 0U; index < buffer.count; ++index) {
        if (buffer.commands[index].source == source) return &buffer.commands[index];
    }
    return nullptr;
}

[[nodiscard]] bool append_rect(
    RenderDamagePlan& plan,
    LogicalRect rect) noexcept {
    if (!rect.bounded()) return false;
    for (std::size_t index = 0U; index < plan.count; ++index) {
        if (plan.rects[index] == rect) return true;
    }
    if (plan.count >= plan.rects.size()) {
        plan.count = 0U;
        plan.full_redraw = true;
        return true;
    }
    plan.rects[plan.count] = rect;
    ++plan.count;
    return true;
}

} // namespace

os::core::Result<RenderDamagePlan> plan_render_damage(
    const RenderCommandBuffer& previous,
    const RenderCommandBuffer& next,
    const RendererDelta& delta) noexcept {
    if (!command_buffer_valid(previous) || !command_buffer_valid(next) ||
        !delta_valid(delta) || delta.revision != next.revision ||
        (previous.revision != 0U && previous.revision > next.revision)) {
        return ui_error(errors::invalid_render_damage);
    }

    RenderDamagePlan plan{.revision = next.revision};
    if (previous.revision == 0U || delta.full_resync_required) {
        plan.full_redraw = true;
        return plan;
    }

    for (std::size_t index = 0U; index < delta.changed_count; ++index) {
        const UiNodeId id = delta.changed[index];
        const RenderCommand* old_command = find_command(previous, id);
        const RenderCommand* new_command = find_command(next, id);

        // A changed semantic-only node may have no renderer command in either
        // frame. It is meaningful to accessibility/input but causes no pixels.
        if (old_command != nullptr && !append_rect(plan, old_command->bounds)) {
            return ui_error(errors::invalid_render_damage);
        }
        if (plan.full_redraw) return plan;
        if (new_command != nullptr && !append_rect(plan, new_command->bounds)) {
            return ui_error(errors::invalid_render_damage);
        }
        if (plan.full_redraw) return plan;
    }

    for (std::size_t index = 0U; index < delta.removed_count; ++index) {
        const UiNodeId id = delta.removed[index];
        const RenderCommand* old_command = find_command(previous, id);
        const RenderCommand* new_command = find_command(next, id);
        if (new_command != nullptr) return ui_error(errors::invalid_render_damage);
        if (old_command != nullptr && !append_rect(plan, old_command->bounds)) {
            return ui_error(errors::invalid_render_damage);
        }
        if (plan.full_redraw) return plan;
    }

    return plan;
}

} // namespace os::ui
