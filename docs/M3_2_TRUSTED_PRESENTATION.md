# M3.2 — compositor-owned trusted presentation

ENML must not let an application mint trusted-system appearance by drawing pixels that resemble shell or secure-system UI.

M3.0 already restricts `system_chrome` surfaces to the trusted shell principal and `secure_system` surfaces to the trusted secure-UI principal. This M3.2 slice carries that authority forward into the scene snapshot as compositor-derived `TrustedPresentation` metadata.

## Rule

`TrustedPresentation` is not a style token and is not accepted in an application frame submission.

The compositor derives it only after surface-role authorization:

- application -> `none`
- popup -> `none`
- system chrome -> `system_chrome`
- secure system -> `secure_system`

An application may still draw arbitrary pixels inside its own buffer, including pixels that visually imitate another surface. Those pixels do not acquire trusted-presentation metadata.

## Why this is separate from the ENML material system

The normal ENML design system intentionally gives applications semantic access to ordinary color, typography, contour, material and motion roles. Secure attribution cannot be another such role. If a public `StyleTokenId` could request the secure-system treatment, the visual trust signal would be counterfeit-able by construction.

Instead, a later compositor backend can combine:

1. the application/system-owned submitted buffer;
2. compositor-authoritative `TrustedPresentation` metadata;
3. compositor-owned attribution primitives that are outside the submitted buffer.

That allows the trusted cue to remain technically attributable even when normal applications use rich color, translucency, dimensional material and expressive curves.

## Capture and input relationship

This slice preserves the existing rule that secure-system surfaces are not capturable through the scene metadata. Trusted presentation is additional metadata, not a replacement for capture policy, z-order authority, exact owner checks or input hit testing.

The security properties remain independent:

- role authorization decides who may create a trusted surface;
- scene ordering keeps secure-system UI above ordinary content;
- capture policy excludes secure-system surfaces;
- `TrustedPresentation` tells the downstream compositor/render backend which scene entries are eligible for compositor-owned trust attribution;
- application pixels never set that eligibility.

## Reference guidance

The original project references reinforce this separation:

- mobile-device security guidance treats architecture, authentication, cryptography and configuration as distinct security concerns and recommends explicit threat modeling rather than relying on appearance;
- OS/UNIX references emphasize mediated, well-defined boundaries between user programs and privileged system services;
- BlackBerry architecture material highlights isolation of application faults from security/radio code;
- the duress-authentication paper demonstrates why security UX must be designed against an explicit adversary model rather than a simple visual or credential trick;
- C++ guidance on strong types and invariants supports representing trust as typed compositor state rather than an implicit convention hidden in colors or asset names.

See also `docs/REFERENCE_PROJECT_FOUNDATIONS_2026_08_09.md`.

## Next step

The next secure-presentation slice should define the compositor-owned attribution primitive itself. It should be deliberately small and unmistakable, remain meaningful in high-contrast/reduced-transparency modes, and be generated outside application-controlled buffers. The exact visual treatment is not frozen yet because it must fit ENML's original visual language rather than imitate an existing platform's security chrome.
