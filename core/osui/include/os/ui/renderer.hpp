#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/ui/contour.hpp>
#include <os/ui/design.hpp>
#include <os/ui/tree.hpp>

namespace os::ui {

// Quality is an implementation budget, not a different visual identity.
// Economy lowers optical cost while preserving geometry, hierarchy and state.
enum class VisualQualityTier : std::uint8_t {
    economy = 1U,
    balanced = 2U,
    full = 3U,
};

struct RenderBuildOptions final {
    VisualPreferences preferences {};
    std::uint16_t text_scale_percent {100U};
    VisualQualityTier quality {VisualQualityTier::balanced};
};

enum class RenderContentKind : std::uint8_t {
    none = 0U,
    text = 1U,
    image_slot = 2U,
    control = 3U,
    collection = 4U,
};

// One command describes one visible styled semantic node. It deliberately
// remains above concrete paint/GPU commands: no RGB values, shader handles,
// glyph IDs, textures, physical pixels or vendor graphics API objects appear
// here. Later renderer slices lower this bounded intent into concrete drawing.
struct RenderCommand final {
    UiNodeId source {};
    UiNodeId parent {};
    std::uint8_t depth {0U};
    UiRole role {UiRole::container};
    LogicalRect bounds {};
    UiNodeState state {};
    RenderContentKind content {RenderContentKind::none};
    ResolvedVisualStyle visual {};
    ResolvedContour contour {};
    TypographyMetrics typography {};

    // Semantic labels are not generally visible text. Until the public UI
    // content contract exists, only UiRole::text is promoted into visual_text.
    SemanticText visual_text {};
    bool focus_visible {false};
};

struct RenderCommandBuffer final {
    std::uint64_t revision {0U};
    std::array<RenderCommand, max_ui_nodes> commands {};
    std::size_t count {0U};
};

[[nodiscard]] os::core::Result<RenderCommandBuffer> build_render_commands(
    const RendererSnapshot& snapshot,
    RenderBuildOptions options = {}) noexcept;

} // namespace os::ui
