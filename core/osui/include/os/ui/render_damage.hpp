#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/ui/renderer.hpp>

namespace os::ui {

inline constexpr std::size_t max_render_damage_rects = 32U;

struct RenderDamagePlan final {
    std::uint64_t revision {0U};
    std::array<LogicalRect, max_render_damage_rects> rects {};
    std::uint8_t count {0U};
    bool full_redraw {false};
};

// Converts SemanticTree dirty/removal metadata into bounded logical redraw
// regions by comparing the previous and next deterministic command buffers.
// Moved/resized nodes damage both their old and new bounds; nodes that vanish
// because they became hidden damage their old bounds; semantic-only nodes that
// emitted no command in either frame cause no pixel work. If the bounded damage
// budget is exceeded, callers receive an explicit full-redraw fallback rather
// than an unbounded region list or heap allocation.
[[nodiscard]] os::core::Result<RenderDamagePlan> plan_render_damage(
    const RenderCommandBuffer& previous,
    const RenderCommandBuffer& next,
    const RendererDelta& delta) noexcept;

} // namespace os::ui
