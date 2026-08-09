# Reference Notes — 2026-08-09 UI / Perception Sources

These notes capture design implications from the additional UI/UX sources supplied during M3.1. They are evidence for ENML's UI policy and test strategy, not visual-style requirements and not permission to copy a vendor design language.

## Through the Liquid Glass: When UI Transparency Blurs Perception (Mühl & Doan, 2026)

The study uses a static prototype based on Apple's iOS 26 UI kit and usability interviews with 11 university students aged 18–30. Its own scope limitation matters: the test object is static rather than the complete dynamic implementation, so ENML should treat the findings as evidence about transparency/perception generally rather than as a definitive evaluation of Apple's shipping UI.

Useful findings for ENML:

- transparent layer-based interfaces can cause users to misread both control state and function even when content remains legible;
- background colour quantity, specific colour, placement/reference points, and surrounding context can alter perceived control state;
- learned UI convention can dominate familiarity with a specific product; in the study, a solid fill was often interpreted as an active state even when that interpretation was wrong;
- familiarity alone did not eliminate state misperception among participants already using the design language;
- visually adjacent controls can become reference points that change how neighbouring controls are interpreted;
- transparency therefore needs evaluation for perceived affordance and state comprehension, not only readability/contrast.

ENML implications:

1. The compositor must remain a mechanism layer. M3 surface/buffer APIs carry ownership, geometry, role and pixels; they must not bake a translucent material or vendor visual language into the stable display ABI.
2. M3.2+ semantic UI/design-system controls should expose explicit logical state independently of material/background rendering.
3. System and secure-system UI should favor unambiguous state presentation. Security-critical controls must not rely on background-transmitted colour or colour alone to distinguish active/inactive or allow/deny states.
4. Transparency over active/colourful content should be constrained by the design system and covered by usability/accessibility tests. Dynamic adaptation may improve visibility but cannot be assumed to solve affordance ambiguity without evidence.
5. Screenshot/secure-UI policy remains semantic. A visually transparent surface does not imply that underlying protected content becomes capturable or that secure-system provenance changes.

## Innovations in UI/UX Design of Mobile Applications: Trends, Practices and Challenges (Jamal & Hashmat, 2025)

This paper is a review of 20 selected publications from 2017–2024 using a PRISMA-style process. ENML uses it as broad secondary evidence rather than as a normative platform specification.

Useful direction:

- user-centered design, iterative testing, performance optimization and inclusive accessibility recur as major mobile UI/UX practices;
- mobile interfaces face constrained screen area, cognitive load, device variability and the need for consistent interaction patterns;
- mobile-first design emphasizes simplicity and responsive adaptation rather than treating a phone as a shrunken desktop;
- accessibility should be considered during initial design, including screen-reader support and adjustable text, rather than retrofitted after visual implementation;
- small responsive feedback/micro-interactions can improve perceived responsiveness, but motion still carries runtime/power cost and should remain purposeful;
- the review discusses energy-aware choices such as lightweight implementation and restrained animation. ENML should validate those claims with its own profiling instead of assuming a visual feature is automatically power efficient.

ENML implications:

1. M3 display infrastructure stays bounded: fixed surface/buffer tables, explicit byte budgets, bounded damage lists and no hidden renderer thread pool before measurement requires one.
2. M3.2 semantic UI must carry accessibility/state information above the pixel buffer. Accessibility is not recoverable reliably from compositor pixels.
3. Responsive layout will use safe insets and semantic layout constraints already represented by `DisplayConfiguration`; applications should not infer physical device classes from Linux display nodes.
4. Animation/motion APIs should eventually express intent and timing through the framework so the platform can coalesce, reduce or disable motion for accessibility and power policy.
5. UI evaluation gates should test task success, state comprehension, large text, contrast/non-colour cues and degraded performance, not only screenshot appearance.

## Interaction with existing references

- BlackBerry and One UI material continues to support workflow-first, standard-component, reachable and responsive phone UX.
- Stroustrup continues to guide move-only resource ownership and type-rich lightweight abstractions in the compositor/buffer code.
- Linux/Bootlin material continues to guide the private graphics/BSP boundary: Linux mechanisms such as memfd today and DRM/KMS/DMA/display drivers later stay below ENML's stable semantic display API.
- ARM material remains relevant to native AArch64 validation and future measured low-level graphics hot paths, but assembly is not the default compositor implementation language.

## M3.1 decision recorded

The new references strengthen, rather than change, the current M3.1 architecture: finish trusted compositor ownership, bounded shared buffers, generation-safe semantic object ids, restart revocation and native ARM64 validation first. Visual materials, transparency, adaptive components and accessibility semantics belong in later semantic UI/design-system layers where they can be tested as user-facing behavior instead of becoming permanent low-level buffer ABI.
