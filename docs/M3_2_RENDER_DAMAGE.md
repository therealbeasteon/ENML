# M3.2 — bounded render damage planning

This slice connects semantic dirty/removal metadata to an explicit bounded redraw plan so small UI changes do not require a full-screen CPU reraster by default.

## Contract

`plan_render_damage(previous, next, delta)` consumes:

- the previous deterministic `RenderCommandBuffer`;
- the next deterministic `RenderCommandBuffer`;
- the `RendererDelta` produced by `SemanticTree` for the next revision.

It returns at most 32 logical Q6 damage rectangles plus an explicit `full_redraw` fallback.

## Behavior

- a moved/resized renderable node damages both old and new bounds;
- a style/text/state change with unchanged geometry damages that rectangle once;
- a renderable node that becomes hidden/unstyled damages its old bounds;
- a removed renderable node damages its old bounds;
- a semantic-only dirty/removed node that emitted no command causes no pixel work;
- duplicate damage rectangles are suppressed;
- fabricated duplicate source IDs, invalid revisions and inconsistent removed IDs fail closed;
- if the fixed region budget is exceeded or the tree reports `full_resync_required`, the plan becomes a full redraw instead of allocating an unbounded region list.

## Performance and power intent

This does not yet implement scissored framebuffer painting. It establishes the bounded work-selection contract that the CPU renderer and later compositor/GPU backends can consume.

A small label/state/focus change can now be represented as local logical damage rather than treating the whole display as dirty. That is important for the original ENML priorities of immediate responsiveness, low CPU work and power efficiency.

The planner itself owns no cache thread, timer or background worker. It runs only when semantic state has already changed.

## Security/correctness intent

Damage planning is an optimization, not authority. `RendererSnapshot`/`RenderCommandBuffer` remain the authoritative render state. If bounded dirty metadata is incomplete or overflows, ENML falls back to full redraw rather than risking stale pixels.

Removed/hidden nodes use old command geometry so their previous pixels are not accidentally retained. A removed ID that still appears in the next command buffer is rejected as an inconsistent input rather than guessed around.

## Next integration

The next concrete renderer step is to let the opaque/text CPU frame path consume these logical regions as clipping/scissor input while proving that depth, antialias fringe and text glyph coverage expand damage conservatively enough for every effect they touch.

That expansion must remain bounded and deterministic. Live blur/backdrop effects will later need larger effect-specific damage inflation and should not be enabled until that rule is explicit.
