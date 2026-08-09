# M3.2 — compositor-owned trusted presentation

ENML must not let an application mint trusted-system appearance by drawing pixels that resemble shell or secure-system UI.

M3.0 already restricts `system_chrome` surfaces to the trusted shell principal and `secure_system` surfaces to the trusted secure-UI principal. M3.2 carries that authority forward into the scene snapshot as compositor-derived `TrustedPresentation` metadata and now into a separate compositor-owned overlay command stream.

## Rule

`TrustedPresentation` is not a style token and is not accepted in an application frame submission.

The compositor derives it only after surface-role authorization:

- application -> `none`
- popup -> `none`
- system chrome -> `system_chrome`
- secure system -> `secure_system`

An application may still draw arbitrary pixels inside its own buffer, including pixels that visually imitate another surface. Those pixels do not acquire trusted-presentation metadata.

## Trusted overlay primitive

`TrustedOverlaySnapshot` is the first non-buffer trust-attribution primitive. It contains only bounded entries for visible, framed scene surfaces whose compositor-derived role and `TrustedPresentation` classification agree.

Each `TrustedOverlayEntry` carries only:

- the authoritative `SurfaceId`;
- `TrustedPresentation` classification;
- compositor-known surface bounds;
- the presented frame sequence.

It does **not** contain an application-supplied color, icon, texture, string, shader or geometry recipe. The eventual display/render backend owns the exact ENML visual trust mark and paints it in a separate compositor-owned pass after client buffers are composed.

`build_trusted_overlay_snapshot()` also repeats the role/classification consistency check. An inconsistent scene record such as `application + secure_system` is not promoted into the overlay. This is defense in depth; the primary authority remains compositor surface-role authorization.

Only visible surfaces with an actual submitted frame are included. Hidden trusted surfaces and trusted surfaces that have not yet presented pixels produce no current overlay work, which keeps the mechanism event-driven and avoids permanent decorative/background activity.

## Why this is separate from the ENML material system

The normal ENML design system intentionally gives applications semantic access to ordinary color, typography, contour, material and motion roles. Secure attribution cannot be another such role. If a public `StyleTokenId` could request the secure-system treatment, the visual trust signal would be counterfeit-able by construction.

The compositor path now separates:

1. application/system-owned submitted buffers;
2. compositor-authoritative `TrustedPresentation` metadata;
3. `TrustedOverlaySnapshot`, generated outside application-controlled buffers;
4. a future hardware/display backend implementation of the ENML trust mark.

That allows the trusted cue to remain technically attributable even when normal applications use rich color, translucency, dimensional material and expressive curves.

## Capture and input relationship

Secure-system surfaces remain non-capturable through scene metadata. Trusted presentation is additional state, not a replacement for capture policy, z-order authority, exact owner checks or input hit testing.

The security properties remain independent:

- role authorization decides who may create a trusted surface;
- scene ordering keeps secure-system UI above ordinary content;
- capture policy excludes secure-system surfaces;
- `TrustedPresentation` decides which scene entries are eligible for trust attribution;
- `TrustedOverlaySnapshot` turns that eligibility into a separate compositor-owned render input;
- application pixels never set either value.

## Visual-language constraint

The exact mark remains deliberately unfrozen. It must be unmistakable but small, work in high-contrast and reduced-transparency modes, remain visible when premium effects are disabled, and fit ENML's original classic/crafted visual language rather than imitate Android, iOS, Knox, BlackBerry, Windows or another platform's secure chrome.

This is why the current primitive carries authority and geometry but no vendor-looking presentation recipe.

## Reference guidance

The project references reinforce this separation:

- mobile-device security guidance treats architecture, authentication, cryptography and configuration as distinct security concerns and recommends explicit threat modeling rather than relying on appearance;
- OS/UNIX references emphasize mediated, well-defined boundaries between user programs and privileged system services;
- BlackBerry architecture material highlights isolation of application faults from security/radio code;
- the duress-authentication paper demonstrates why security UX must be designed against an explicit adversary model rather than a simple visual or credential trick;
- C++ guidance on strong types and invariants supports representing trust as typed compositor state rather than an implicit convention hidden in colors or asset names.

See also `docs/REFERENCE_PROJECT_FOUNDATIONS_2026_08_09.md`.

## Next step

The next secure-presentation work should connect `TrustedOverlaySnapshot` to the concrete compositor/display backend once that hardware/private layer exists, then define and usability-test the actual ENML trust mark. The visual treatment must remain compositor-owned and must never become an application style token.
