# ENML UI reference guidance — 2026-08-09

This note records how the supplied UI/UX references guide ENML without becoming a visual template. The project vision remains authoritative: ENML must be original, classic, crafted and luxurious; dimensional rather than flat by default; rich in color, authored curves, material depth and purposeful motion; and capable of transparency/translucency without sacrificing clarity, security, accessibility or bounded performance.

## Reference discipline

References answer why a design technique works, what failure modes have already been observed, and which interaction invariants deserve protection. They do not define ENML component silhouettes, icon sets, palettes, fonts, transition signatures or layout chrome.

Familiar interaction semantics may be reused where they lower cognitive load. Vendor visual grammar must not be reproduced. In particular, ENML must not become a BlackBerry, Windows Phone, Microsoft Design Language, Android/Material, Kinect, iOS/Liquid Glass, One UI or UX-pattern-book reskin.

## Guidance extracted from the supplied sources

### BlackBerry Smartphones 7.1 UI Guidelines

Use the enduring interaction guidance: keep important state visible, keep flows concise, provide feedback, avoid dead ends, support recovery, use confirmation for critical tasks, apply progressive disclosure, and respect localization/accessibility. The material is also useful evidence that perceived craftsmanship can come from depth, lighting, highlights, shadow and material cues. ENML must not copy BlackBerry components, icons, historical chrome or theme assets.

### Windows Phone 7 UI Design and Interaction Guide

Use the interaction discipline rather than Metro appearance. Controls should make their purpose understandable; hints should be short and contextual; platform theme and power implications matter; input, orientation, navigation, transitions and system controls need consistent behavior. ENML must not copy the Windows Phone application bar, tile language, typography, icon set or screen composition.

### Microsoft Design Language Style Guide

Motion is guidance, not decoration: it should orient the user, reinforce the mental model, communicate hierarchy and register changes in system state. Typography must remain clear while also controlling density and hierarchy. Icons should harmonize with the type and the rest of the system, communicate only what is needed, and scale coherently. Contrast/readability must be treated as a design constraint even when the palette is rich. ENML may apply these principles while using its own geometry, motion curves, typography and palette.

### Kinect for Windows Human Interface Guidelines

Use the interaction-design lessons around discoverability, immediate feedback, clear state communication, low fatigue, multimodal input and feedback that maps directly to the action being performed. ENML should apply these ideas to touch, keyboard, pointer, voice and later sensor input without inheriting Kinect-specific layouts or gesture visuals.

### The Basics of User Experience Design

Treat usability, interaction design, motion/feedback, user-centered design, testing and desirability as one product-design problem rather than separate polish phases. ENML visual ambition is successful only when the user can still understand, predict and recover from interaction.

### Spero & Biddle — security mental models

Security state must be visible and intelligible, attention must be treated as scarce, and users should be prepared to make security decisions without being forced to become security experts. Observable cause/effect relationships should help users build a useful mental model over time. ENML secure-system UI therefore needs clear trusted attribution and understandable security state, not hidden mechanisms or constant alarm-style warnings.

### Through the Liquid Glass: When UI Transparency Blurs Perception

Transparency is not automatically premium or usable. Layered transparency can confuse figure/ground separation and alter the perceived state or function of controls depending on color quantity, background context, reference points and conventions. There is no single transparency percentage that is correct for every interface. ENML must therefore treat transparency as authored material with context-sensitive contrast and state cues, not as a blanket glass effect.

### Mobile App UX Principles

Protect the complete user journey: adoption, use, transaction/task completion and return. Remove roadblocks, provide convenience, make important decisions understandable, and use delight in support of the task. ENML system surfaces should minimize avoidable friction without hiding meaningful state.

### Design of a Mobile Application

A design system is a combination of principles, patterns and practices used to create a consistent and robust product. ENML should reuse semantic components and tokens rather than redesigning each screen independently, while leaving enough renderer freedom for authored visual expression.

### Innovations in UI/UX Design of Mobile Applications

Keep user-centered iteration, accessibility, responsive composition and performance optimization together. Rich or emerging visual techniques need bounded implementation cost and graceful fallback on weaker devices. ENML economy/balanced/full quality policy exists to lower optical cost without switching to a generic second theme.

### UXPin Mobile UI Design Patterns

Patterns solve recurring user problems; example implementations are not components to copy as-is. Gestures, animation, discoverable controls, transparency, content layers and direct manipulation are a vocabulary to evaluate, not a visual kit. ENML should deliberately break from established visual patterns when doing so preserves usability and creates a coherent original system.

### Android UI Design

Use the architectural lessons around reusable components, semantic hierarchy, separation of structure from appearance, density-independent layout, responsive adaptation, event contracts and bounded list recycling. ENML does not adopt Android Activities, XML resources, Material components or Android visual identity.

### UI/UX Design with Figma

Figma is part of the design/developer workflow: wireframes, reusable components, prototyping, layout exploration, opacity, gradients, curves, blur, shadows and collaboration. The Figma file must express ENML tokens and components; it must not become the source of a copied vendor style.

## ENML design constraints derived from the references

1. **Figure/ground before glass.** A translucent control must remain identifiable against every permitted background. State cannot depend on background accident.
2. **Material hierarchy before effect density.** Opaque, translucent, crystal, smoked and luminous roles have different jobs. Not every surface should be transparent.
3. **Authored contours before generic rounded rectangles.** Continuous and swept geometry should contribute to recognition even when color and blur are removed.
4. **Motion must communicate cause and effect.** It should orient, reveal hierarchy, connect origin/destination and register system changes. Decorative delay is a defect.
5. **Color is expressive but redundant.** Focus, selection, critical state and trust attribution also need geometry, text, iconography or motion cues.
6. **Typography is both identity and infrastructure.** ENML can have an authored display character, but clarity, scalable metrics, international fallback and large-text reflow are non-negotiable.
7. **Security is understandable, not invisible.** Trusted system state must be attributable and intelligible without consuming constant attention.
8. **Accessibility is an alternate expression of the same language.** Reduced transparency, reduced motion, high contrast and large text must preserve ENML identity.
9. **Performance is a design input.** Optical complexity is explicitly budgeted; weaker hardware lowers cost rather than changing the visual grammar.
10. **Prototype evidence does not outrank implementation truth.** A Figma or generated preview can explore direction, but only the renderer/compositor output is treated as an actual ENML render.

## Current implementation mapping

M3.2 already reflects these constraints through semantic UI independent of pixels, explicit accessibility projection, logical geometry/reflow, stable collection identity, renderer snapshots, semantic color/type/material/depth/curve/motion roles, visual preferences, quality/capability fallback, deterministic render commands, bounded shaping output and renderer-owned font/shaper seams.

The first concrete pixel stage is now an opaque-first bounded CPU rasterizer. It accepts a renderer-owned concrete theme, maps Q6 logical geometry to a caller-owned pixel target, applies semantic background/material tint, preserves per-corner asymmetric contour geometry and renders explicit outline/focus treatment. It intentionally does not implement live blur, backdrop sampling, font glyph masks or GPU shaders yet. This order protects figure/ground, affordance and ENML contour identity before expensive optical effects are introduced.

Next visual work should build on that baseline: production contour smoothing/path coverage, renderer-owned font assets and shaping, paragraph bidi/line breaking, text glyph rasterization, depth/lighting primitives, secure-system attribution, then bounded translucency/backdrop effects and compositor-deadline-aware motion.
