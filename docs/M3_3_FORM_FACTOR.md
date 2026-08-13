# M3.3 — Form factor, density and safe area

Phones are not one shape. Cookie has to run on a 5-inch 720×1600 budget panel
and a 6.9-inch 1440×3200 flagship, on 19:9 and 20:9 and 21:9, on panels with a
centre punch-hole, a corner punch-hole, a notch, a curved edge, rounded corners
of varying radius, and on devices that fold and change window size while an
application is running.

This document defines how Cookie handles that. It is a contract, not a
suggestion: the current M3.2 UI layer has no model for any of it, and that gap
should be closed before the visual language freezes rather than retrofitted
after.

## What already exists, and why it is the right base

M3.2 chose fixed-point logical geometry: **64 units == 1 density-independent
unit**. That decision is the foundation this document builds on and it does not
change. Layout is already expressed in a unit that is not a physical pixel,
which is the hard part; what is missing is everything that connects that unit to
an actual panel.

What does not exist today, and must:

- no display metrics — no physical size, no pixel density, no scale factor;
- no size classes, so a layout cannot ask whether it has a phone's width or a
  folded tablet's;
- no safe-area or cutout model, so nothing can avoid a punch-hole;
- no notion that any of this can change while an application is running.

## The rules

### 1. Applications receive a semantic window description, never panel details

An application is told the size class, the safe insets and the accessibility
scale of the window it occupies. It is never told the panel's resolution, its
physical dimensions, its density bucket, its cutout coordinates, its refresh
rate or its model.

This follows the existing rule that renderer internals stay platform-private,
and it is a privacy boundary as much as an architectural one: exact panel
metrics, refresh rate and cutout geometry are a strong fingerprinting surface,
and an OS whose charter is privacy-first should not hand every application a
device-identifying tuple simply to let it lay out a button. The compositor knows
the panel. The application knows its window.

### 2. Size classes are ranges, never device identities

Three classes, on logical width, with height considered second:

| Class | Logical width | Typical |
| --- | --- | --- |
| `compact` | < 600 | phones in portrait, folded devices, cover displays |
| `medium` | 600–839 | small tablets, unfolded portrait, phones in landscape |
| `expanded` | >= 840 | unfolded landscape, tablets, external displays |

The numbers are conventional because interoperability with designer intuition
is worth more here than novelty; the *boundaries* are borrowed, the visual
grammar they drive is not, and `PROJECT_VISION.md`'s non-derivative rule is
about the latter.

Height matters independently. A landscape phone and an unfolded tablet can share
a width class and need different vertical treatment, so a window carries a height
class over the same thresholds. Layout decides on width first, then height.

**A size class is not a device type.** Nothing may branch on "is this a phone".
The same window may pass through all three classes in one session, and code that
asked what device it was on will be wrong in the middle of a fold.

### 3. Safe insets are mandatory and unconditional

Every window carries four insets describing where content may be drawn without
being clipped by a cutout, a rounded corner, a curved edge, or system chrome.

Cookie draws edge-to-edge by default — the background extends under the whole
panel — while interactive and legible content stays inside the insets. Two
inset sets, because they answer different questions:

- **display insets**: physical obstruction. Cutouts, corner radius, curved edge.
- **system insets**: trusted-system chrome. Status area, navigation affordance,
  the secure attribution mark.

A component that ignores insets is a bug, not a style choice. The one place this
is enforceable rather than documented is the layout engine, so container
components apply insets by default and opting out is explicit and reviewed.

### 4. Density is the compositor's problem, not the application's

The application lays out in logical units. The compositor multiplies by a scale
factor it owns and derives from the panel. Applications never see the factor.

Scale is continuous, not bucketed. Density buckets are a compatibility artefact
of shipping raster assets at fixed multiples, and Cookie's visual language is
already committed to authored curves and contours rather than bitmap assets —
`M3_2_CONTOUR_ANTIALIAS.md` exists precisely because shapes are rendered rather
than blitted. A system that resolves shapes at the output resolution has no
reason to quantise the panel into 2x and 3x, and quantising costs sharpness on
every panel that is not exactly at a bucket.

### 5. Accessibility scale is separate from density scale

Density scale maps logical units to physical pixels. Accessibility scale is the
user's text-size preference. They multiply, and they are not interchangeable:
raising text size must reflow layout, while raising density must not.

Because text can grow independently, no layout may assume a text box's height.
Any fixed-height container holding text is a defect that will appear at the
largest accessibility setting and nowhere else, which is precisely why it needs
a gate rather than review — see below.

### 6. Window size changes mid-run, and the window generation says so

A fold, an unfold, a rotation and a window resize are the same event: the window
description changed. It carries a generation, and a frame submitted against a
stale generation fails closed rather than being scaled or letterboxed into the
new one. That is the same rule the compositor already applies to stale scene
revisions; this adds nothing new to the model.

There is no "resizable" opt-out. An application that cannot handle its window
changing is an application that breaks when the device folds, and letting it
declare that as a manifest flag makes the platform carry the compatibility
burden forever. Cookie is new; it has no legacy applications to protect, and
this is the one moment where refusing the flag is free.

### 7. Aspect ratio is never assumed

No layout may hardcode an aspect ratio, and no component may assume its parent's.
Panels in current production span roughly 16:9 to 21:9, and folded/unfolded
states move across that range in one motion.

## Exit criteria

M3.3 is complete when:

- a `WindowDescription` type carries size classes, both inset sets,
  accessibility scale and a generation, and reaches applications through the
  existing semantic UI boundary;
- the compositor owns density scale and no public API exposes it;
- container components apply safe insets by default;
- layout is verified at the extremes rather than at a nominal size: the
  narrowest and widest supported logical widths, the shortest height, the
  largest accessibility scale, and a fold transition mid-frame;
- a gate fails when a component clips or overflows at maximum accessibility
  scale, because that is the failure this document exists to prevent and it is
  invisible at default settings;
- stale-generation frames fail closed, with a test.

## What this does not do

It does not describe external displays, desktop windowing, multi-window on one
panel, or stylus and pointer input. Those are real and they are later; naming
them here is to record that the size-class model was chosen to extend to them
rather than to be replaced by them.

It does not settle the visual grammar at each class. `M3_2_ENML_VISUAL_LANGUAGE.md`
owns that, and the non-derivative rule applies unchanged: borrowing where a
breakpoint falls is not borrowing what the interface looks like on either side
of it.
