#include <os/ui/renderer.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <os/ui/error.hpp>

namespace os::ui {
namespace {

[[nodiscard]] constexpr bool role_valid(UiRole role) noexcept {
    switch (role) {
    case UiRole::root:
    case UiRole::container:
    case UiRole::text:
    case UiRole::image:
    case UiRole::button:
    case UiRole::toggle:
    case UiRole::text_field:
    case UiRole::list:
    case UiRole::list_item:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool quality_valid(VisualQualityTier quality) noexcept {
    switch (quality) {
    case VisualQualityTier::economy:
    case VisualQualityTier::balanced:
    case VisualQualityTier::full:
        return true;
    }
    return false;
}

[[nodiscard]] const UiNodeDescriptor* find_node(
    const RendererSnapshot& snapshot,
    UiNodeId id) noexcept {
    if (id.value() == 0U) return nullptr;
    for (std::size_t index = 0U; index < snapshot.count; ++index) {
        if (snapshot.nodes[index].id == id) return &snapshot.nodes[index];
    }
    return nullptr;
}

[[nodiscard]] bool snapshot_valid(const RendererSnapshot& snapshot) noexcept {
    if (snapshot.count == 0U || snapshot.count > max_ui_nodes) return false;

    std::size_t root_count = 0U;
    for (std::size_t index = 0U; index < snapshot.count; ++index) {
        const UiNodeDescriptor& node = snapshot.nodes[index];
        if (node.id.value() == 0U || !role_valid(node.spec.role) ||
            node.depth > max_ui_depth || !node.spec.bounds.bounded() ||
            !semantic_text_valid(node.spec.label) ||
            !style_token_valid(node.spec.style)) {
            return false;
        }

        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (snapshot.nodes[earlier].id == node.id) return false;
        }

        if (node.spec.role == UiRole::root) {
            ++root_count;
            if (node.parent.value() != 0U || node.depth != 0U) return false;
            continue;
        }

        if (node.parent.value() == 0U || node.depth == 0U) return false;
        const UiNodeDescriptor* parent = find_node(snapshot, node.parent);
        if (parent == nullptr || parent->depth + 1U != node.depth) return false;
    }

    if (root_count != 1U) return false;

    // Prove every non-root parent chain terminates at the single root within
    // the bounded semantic depth; this rejects fabricated cycles/snapshots.
    for (std::size_t index = 0U; index < snapshot.count; ++index) {
        const UiNodeDescriptor* current = &snapshot.nodes[index];
        for (std::uint8_t steps = 0U; steps <= max_ui_depth; ++steps) {
            if (current->spec.role == UiRole::root) break;
            current = find_node(snapshot, current->parent);
            if (current == nullptr) return false;
            if (steps == max_ui_depth) return false;
        }
        if (current->spec.role != UiRole::root) return false;
    }

    return true;
}

[[nodiscard]] bool effectively_visible(
    const RendererSnapshot& snapshot,
    const UiNodeDescriptor& node) noexcept {
    if (!node.spec.state.visible) return false;

    const UiNodeDescriptor* current = &node;
    for (std::uint8_t steps = 0U; steps <= max_ui_depth; ++steps) {
        if (current->spec.role == UiRole::root) return current->spec.state.visible;
        current = find_node(snapshot, current->parent);
        if (current == nullptr || !current->spec.state.visible) return false;
    }
    return false;
}

[[nodiscard]] constexpr RenderContentKind content_kind(UiRole role) noexcept {
    switch (role) {
    case UiRole::root:
    case UiRole::container:
        return RenderContentKind::none;
    case UiRole::text:
        return RenderContentKind::text;
    case UiRole::image:
        return RenderContentKind::image_slot;
    case UiRole::button:
    case UiRole::toggle:
    case UiRole::text_field:
        return RenderContentKind::control;
    case UiRole::list:
    case UiRole::list_item:
        return RenderContentKind::collection;
    }
    return RenderContentKind::none;
}

void apply_quality_budget(
    ResolvedVisualStyle& visual,
    VisualQualityTier quality) noexcept {
    if (quality == VisualQualityTier::full) return;

    if (quality == VisualQualityTier::balanced) {
        visual.material.backdrop_blur_q6 = std::min(
            visual.material.backdrop_blur_q6,
            logical_from_dp(20U));
        visual.depth.blur_q6 = std::min(
            visual.depth.blur_q6,
            logical_from_dp(24U));
        return;
    }

    // Economy keeps the authored contour/material family and hierarchy but
    // removes live backdrop work and bounds secondary optical flourishes.
    visual.material.backdrop_blur_q6 = 0U;
    visual.material.live_backdrop_allowed = false;
    visual.material.specular_percent = std::min<std::uint8_t>(
        visual.material.specular_percent,
        8U);
    visual.depth.blur_q6 = std::min(
        visual.depth.blur_q6,
        logical_from_dp(8U));
}

} // namespace

os::core::Result<RenderCommandBuffer> build_render_commands(
    const RendererSnapshot& snapshot,
    RenderBuildOptions options) noexcept {
    if (!quality_valid(options.quality)) {
        return ui_error(errors::invalid_render_options);
    }
    if (options.text_scale_percent < min_text_scale_percent ||
        options.text_scale_percent > max_text_scale_percent) {
        return ui_error(errors::invalid_text_scale);
    }
    if (!snapshot_valid(snapshot)) {
        return ui_error(errors::invalid_render_snapshot);
    }

    std::array<const UiNodeDescriptor*, max_ui_nodes> ordered {};
    for (std::size_t index = 0U; index < snapshot.count; ++index) {
        ordered[index] = &snapshot.nodes[index];
    }
    for (std::size_t index = 1U; index < snapshot.count; ++index) {
        const UiNodeDescriptor* current = ordered[index];
        std::size_t position = index;
        while (position > 0U &&
               ordered[position - 1U]->id.value() > current->id.value()) {
            ordered[position] = ordered[position - 1U];
            --position;
        }
        ordered[position] = current;
    }

    RenderCommandBuffer buffer {};
    buffer.revision = snapshot.revision;

    for (std::size_t index = 0U; index < snapshot.count; ++index) {
        const UiNodeDescriptor& node = *ordered[index];
        if (!effectively_visible(snapshot, node)) continue;

        // StyleTokenId zero intentionally means semantic-only / unstyled. It
        // remains in accessibility and interaction snapshots but emits no
        // renderer command.
        if (node.spec.style.value() == 0U) continue;

        auto visual_result = resolve_visual_style(
            node.spec.style,
            options.preferences);
        if (!visual_result) return visual_result.error();
        ResolvedVisualStyle visual = visual_result.value();
        apply_quality_budget(visual, options.quality);

        auto contour_result = resolve_contour(node.spec.bounds, visual.token.curve);
        if (!contour_result) return contour_result.error();

        auto text_style_result = resolve_text_style(
            visual.token.typography,
            options.text_scale_percent);
        if (!text_style_result) return text_style_result.error();

        SemanticText visual_text {};
        if (node.spec.role == UiRole::text) visual_text = node.spec.label;

        buffer.commands[buffer.count] = RenderCommand{
            .source = node.id,
            .parent = node.parent,
            .depth = node.depth,
            .role = node.spec.role,
            .bounds = node.spec.bounds,
            .state = node.spec.state,
            .content = content_kind(node.spec.role),
            .visual = visual,
            .contour = contour_result.value(),
            .typography = text_style_result.value().metrics,
            .font_fallbacks = text_style_result.value().fallback,
            .visual_text = visual_text,
            .focus_visible = node.spec.state.focused,
        };
        ++buffer.count;
    }

    return buffer;
}

} // namespace os::ui
