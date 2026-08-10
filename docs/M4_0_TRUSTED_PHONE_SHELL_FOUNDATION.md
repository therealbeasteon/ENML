# M4.0 — trusted phone shell foundation

## Implementation authority

**References teach principles. ENML determines implementation. External systems are not the design specification.**

M4.0 is derived from ENML's own product mission and the contracts completed through M3.2. Supplied references inform bounded ownership, attack-surface reduction, asynchronous/event-driven mobile behavior, explicit threat modeling, usability and trusted-system presentation; they do not prescribe a launcher/task-switcher ABI, visual layout, navigation gesture or process topology.

The shell is deliberately treated as privileged operating-system policy, not as a cosmetic launcher application.

## Why this is the current milestone

M0–M2 established trusted process/service/package/storage/key foundations. M3 established compositor ownership and a bounded semantic UI/render/input/accessibility stack. ENML can render and securely interact with applications; M4.0 turns those capabilities into coherent phone navigation without weakening the authority boundaries underneath them.

## Event-driven task state

`core/osshell` contains a bounded trusted `ShellTaskModel`.

Properties:

- at most 16 live tasks, exactly matching the current App Manager live-instance ceiling rather than creating a larger hidden registry;
- one task is identified by ENML `ApplicationInstanceId`, signed `ApplicationIdentity`, exact live `PeerIdentity` and generation-scoped application root `SurfaceId`;
- no native PID, executable path, Linux window handle or vendor task identifier becomes shell ABI;
- publication is event-driven by trusted lifecycle/display integration; there is no task scanner, `/proc` crawler, process poller or timer;
- a compositor restart may update the generation-scoped root surface for the **same exact** application instance/owner without manufacturing a new task;
- an instance cannot be rebound to a different application/process identity;
- one live exact process/root surface cannot appear as multiple shell tasks;
- activation/MRU serials are shell-owned; publishers cannot inject recency ordering;
- removing the active task returns to Home instead of silently promoting another process;
- Overview preserves the last active instance as navigation context but does not itself grant compositor foreground authority;
- every state mutation is revisioned with explicit exhaustion behavior.

The initial semantic views remain `home`, `application` and `overview`. These names do not prescribe a copied visual layout. The eventual visual shell will be rendered through ENML's own semantic UI/material system.

## Corroborated lifecycle authority

A shell task is not accepted from a single source. `ShellTaskModel::reconcile()` joins:

1. App Manager's bounded `ApplicationLifecycleSnapshot`;
2. compositor scene state for an exact application-root surface.

Only an exact lifecycle identity that also owns exactly one application root becomes a task. Lifecycle-only records and orphan compositor surfaces are omitted. Duplicate identities, contradictory roots, malformed role/trust metadata, lifecycle rollback and older compositor generations fail closed.

App Manager's lifecycle projection is demand-driven and cached. Re-reading unchanged live state returns the same revision and creates no background work.

## Authenticated shell ↔ App Manager boundary

`ShellLifecycleControlServer/Client` provides the cross-process lifecycle transport.

- caller identity is derived from packet `SCM_CREDENTIALS` through the trusted runtime identity resolver;
- only the canonical ENML shell principal is authorized;
- authorization happens before operation/payload-specific processing so an ordinary process cannot use error differences as an application-enumeration oracle;
- the response is bounded and contains semantic application identity plus exact `PeerIdentity`, not native PID, executable path, storage root, service endpoint or package-directory capability;
- package identifiers, signer lineage, identity uniqueness, revision and capacity are revalidated at the boundary;
- malformed or ambiguous lifecycle state is rejected instead of normalized into a plausible shell view.

## Canonical trusted principals

The shell and secure-UI principal labels now live in one low-level `platform_principals.hpp` definition. This avoids independent trusted components silently disagreeing about which principal owns an authority boundary.

These constants are identifiers, not capabilities. Privileged IPC still authenticates the live sending process from kernel credentials.

## Foreground activation authority

`ShellTaskModel::activate()` changes semantic shell intent only. A separate `ShellActivationIntent` binds the current shell revision to:

- application instance;
- signed application identity;
- exact live `PeerIdentity`;
- generation-scoped application root `SurfaceId`.

The intent is revalidated immediately before privileged compositor commit. Any intervening shell revision, navigation, lifecycle owner or root-surface change invalidates the old intent. Home and Overview cannot mint an application activation intent.

The compositor independently provides `activate_application_exact()`. Only the shell principal may invoke it, and the application root must still belong to the exact lifecycle owner carried by the trusted intent. Role/owner mismatch is one generic activation denial rather than an ownership-enumeration mechanism.

A private `ShellCompositorControlServer/Client` now carries that exact-owner request across a bounded authenticated IPC contract. Endpoint possession and knowledge of its numeric ServiceId are not authority; an ordinary kernel sender is denied before expected-owner/SurfaceId parsing. The transport preserves stale generation failure rather than remapping an old `SurfaceId` to a new compositor object.

The current private control contract is ready for the supervised shell/compositor composition slice; it is intentionally not exposed as an application operation.

## Preview privacy and forensic minimization

M4 does not create a recents screenshot database merely to imitate another phone OS.

`TaskPreviewGrant` is an ephemeral authorization record containing only shell revision, exact task identity, generation-scoped root/buffer and frame sequence. It contains no pixels, mapped memory, path, file descriptor, cache key or persistence instruction.

The conservative default policy grants transient sampling only when the exact root is:

- an ordinary application surface;
- currently visible;
- owned by the exact reconciled application identity;
- presenting a live frame;
- explicitly capture-allowed by the compositor;
- not classified as trusted system presentation.

Hidden applications use semantic Overview cards rather than forcing screenshot retention. Popups, system chrome and secure-system presentation are rejected. A shell revision, owner, surface, buffer, frame, visibility, trust or capture-policy change makes an old preview grant stale before capture.

M4.0 therefore introduces **no persistent recents history or preview store**. Full physical/offline forensic resistance remains a later whole-device security track involving verified boot, production hardware-backed keys, recovery/update and encryption/key lifecycle; the shell must not create unnecessary durable artifacts in the meantime.

See `docs/M4_0_SECURITY_EXIT_CRITERIA.md`.

## Resource/power rule

The shell should become quiet when nothing changes. Task/lifecycle updates, navigation actions, compositor frame opportunities and explicit UI invalidation are sufficient triggers. M4.0 does not add:

- a recent-app polling thread;
- a process scanner;
- a permanent animation timer;
- a background thumbnail refresher;
- an unbounded task/event queue.

This follows the supplied mobile architecture principle that polling wastes work/power while ENML retains its own implementation and API design.

## Security rule

Shell presentation is trusted system UI only when backed by exact principal, lifecycle and compositor authority. An application drawing pixels that resemble Home, Overview or system chrome does not become shell UI.

No hidden development/debug path is permitted to bypass exact-principal activation, lifecycle authorization, trusted presentation, capture policy or stale-object validation in the production authority path.

## Validation

M4 shell work has dedicated GCC, Clang, ASan/UBSan and native-AArch64 gates. The current gate set covers:

- task publication/reconciliation and fixed capacities;
- stale lifecycle/compositor-generation rejection;
- authenticated lifecycle transport and unauthorized-caller denial;
- revision-bound activation intent;
- exact-owner compositor activation;
- authenticated private shell compositor activation transport;
- privacy-safe preview grants, hidden/non-capturable/trusted denial and stale-frame rejection.

Shell-dependent compositor behavior also remains inside the M3 display/compositor cross-compiler/sanitizer/AArch64 matrix.

## Remaining M4.0 slices

Continue in this order unless repository evidence exposes a lower-level blocker:

1. supervise the actual trusted shell process and compose its private lifecycle/activation capabilities without exposing a global shell-admin endpoint;
2. compositor-owned system-chrome surface lifecycle for that exact shell principal, including restart/stale-generation recovery;
3. semantic Home/Overview UI model using M3.2 UI tokens, accessibility and original ENML visual language;
4. connect optional transient task sampling to the preview-grant/revalidation policy without adding persistent recents screenshots;
5. compositor-deadline-aware transitions with reduced-motion/economy fallbacks;
6. trusted input/navigation integration without raw hardware APIs;
7. literal framebuffer/render captures and usability/accessibility refinement.

M4.0 does not pull telephony, DRM/KMS/GPU hardware enablement, verified boot, recovery/update, hardware-backed production keys or full power management into the shell merely to make the milestone appear broader. Those remain separate platform tracks and must receive their own hardened designs.
