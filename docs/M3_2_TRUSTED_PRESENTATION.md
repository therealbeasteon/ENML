# M3.2 — compositor-owned trusted presentation

ENML must not let an application mint trusted-system authority merely by drawing pixels that resemble shell or secure-system UI.

M3.0 restricts `system_chrome` surfaces to the trusted shell principal and `secure_system` surfaces to the trusted secure-UI principal. M3.2 carries that authority forward into compositor-derived `TrustedPresentation`, a separate `TrustedOverlaySnapshot`, and now a concrete compositor-owned CPU trust-mark pass.

## Authority rule

`TrustedPresentation` is not a style token and is not accepted in an application frame submission.

The compositor derives it only after surface-role authorization:

- application -> `none`
- popup -> `none`
- system chrome -> `system_chrome`
- secure system -> `secure_system`

An application may draw arbitrary pixels inside its own buffer, including pixels that visually imitate trusted UI. Those pixels do not acquire trusted-presentation metadata and cannot cause the compositor trust-mark pass to execute.

## Trusted overlay primitive

`TrustedOverlaySnapshot` is the bounded non-buffer trust-attribution input. It contains only visible, framed scene surfaces whose compositor-derived role and `TrustedPresentation` classification agree.

Each `TrustedOverlayEntry` carries only the authoritative `SurfaceId`, trusted classification, compositor-known bounds and presented frame sequence. It contains no application-supplied color, icon, string, texture, shader or trust geometry recipe.

`build_trusted_overlay_snapshot()` repeats the role/classification consistency check. An inconsistent scene record such as `application + secure_system` is not promoted into the overlay. Public compositor IPC exposes no operation that accepts caller-created scene/overlay snapshots.

Only visible surfaces with a submitted frame are included. Hidden or not-yet-presented trusted surfaces produce no mark work, preserving event-driven behavior.

## Concrete compositor-owned mark

`rasterize_trusted_marks()` is the first concrete private CPU/display fallback for the overlay. It consumes only `TrustedOverlaySnapshot`, a compositor-private opaque palette, and the final compositor-owned pixel target. It is intended to run **after client composition**.

The baseline ENML signature is deliberately geometric rather than a borrowed lock/shield icon:

- system chrome receives a compact asymmetric top-right corner cradle;
- secure-system presentation uses a distinct compositor-selected accent plus a second rising seam;
- both use an opaque contrasting foundation under the accent so legibility does not depend on trusting or sampling client pixels;
- tiny trusted surfaces still receive bounded attribution rather than silently dropping the cue.

The pass performs no heap allocation, backdrop sampling, shader compilation, font lookup, worker scheduling or animation. Work is bounded by the fixed overlay count and the small mark geometry rather than by full-surface area.

The raster boundary validates target memory/stride/dimensions, opaque/distinct compositor palette entries, generation-scoped surface ids, trusted presentation kinds, nonzero frame sequences and bounds containment before writing pixels. Malformed internal overlay state fails closed through the display error domain.

## What this does and does not prove

The compositor-owned pass creates **technical attribution**: ordinary application pixels cannot request, set or execute the trusted overlay path, and trusted input/surface authority remains independent of the mark.

The visual shape by itself is **not treated as cryptographic proof**. A malicious application can still draw a lookalike pattern inside its own untrusted buffer. ENML therefore must not train users to authorize high-risk actions based only on recognizing a tiny glyph. Secure interactions continue to depend on compositor-authorized secure surfaces, trusted z-order, capture policy and trusted input routing. Future usability work may pair the mark with additional system-owned context where needed.

This distinction follows the project security-UX rule: make trusted state intelligible without pretending appearance can replace authority.

## Why this is separate from the ENML material system

The normal ENML design system gives applications semantic access to ordinary color, typography, contour, material and motion roles. Secure attribution cannot be another such role. If a public `StyleTokenId` could request the secure-system treatment, the trust signal would be counterfeit-able by construction.

The compositor path now separates:

1. submitted application/system buffers;
2. compositor-authoritative `TrustedPresentation`;
3. `TrustedOverlaySnapshot` generated outside application buffers;
4. private compositor-owned trust-mark rasterization after client composition.

The future DRM/KMS/GPU backend must preserve this ordering and authority contract rather than exposing the mark as a public shader/material option.

## Capture and input relationship

Secure-system surfaces remain non-capturable through scene metadata. Trusted presentation is additional state, not a replacement for capture policy, z-order authority, exact owner checks or input targeting.

The security properties remain independent:

- role authorization decides who may create a trusted surface;
- scene ordering keeps secure-system UI above ordinary content;
- capture policy excludes secure-system surfaces;
- `TrustedPresentation` determines overlay eligibility;
- `TrustedOverlaySnapshot` creates the compositor-private render input;
- `rasterize_trusted_marks()` paints the baseline attribution after client buffers;
- application pixels never set any of those trust values.

## Visual-language constraint

The first mark is an implementation baseline, **not a frozen final brand asset**. Its geometry/palette must still be evaluated for recognition, false confidence, high-contrast behavior, reduced-transparency modes, localization-independent comprehension and interaction with future system chrome.

Any refinement must stay original to ENML and must not imitate Android, iOS, Knox, BlackBerry, Windows or another platform's secure chrome. Premium optical effects are optional; the trust cue must remain visible in the opaque economy renderer.

## Reference guidance

The supplied project references support the architectural separation rather than a specific icon. Security-UX material emphasizes intelligible security-relevant state and observable cause/effect, while the broader OS/security references support mediated privileged boundaries. The duress material is also a reminder that visual or credential tricks are not substitutes for an explicit adversary model.

See `docs/REFERENCE_PROJECT_FOUNDATIONS_2026_08_09.md` and `docs/REFERENCE_UI_DESIGN_GUIDANCE_2026_08_09.md`.

## Next step

The next secure-presentation work is to carry the same overlay-after-client ordering into the future private hardware compositor backend and usability-test the baseline mark together with actual secure-system flows. The mark must never become an application style token or caller-supplied compositor command.
