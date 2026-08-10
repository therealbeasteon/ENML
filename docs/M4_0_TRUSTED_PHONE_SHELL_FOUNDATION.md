# M4.0 — trusted phone shell foundation

## Implementation authority

**References teach principles. ENML determines implementation. External systems are not the design specification.**

M4.0 is derived from ENML's own product mission and the contracts completed through M3.2. Supplied references inform bounded ownership, mobile resource discipline, usability and trusted-system presentation; they do not prescribe a launcher/task-switcher ABI, visual layout, navigation gesture or process topology.

## Why this is the next milestone

M0–M2 established trusted process/service/package/storage/key foundations. M3 established compositor ownership and a bounded semantic UI/render/input/accessibility stack. ENML can now render and securely interact with applications, but it still needs an ENML-owned trusted phone shell that turns those capabilities into coherent product navigation.

M4.0 starts with task/navigation authority rather than visual imitation of an existing launcher.

## First foundation: event-driven task state

`core/osshell` introduces a bounded trusted `ShellTaskModel`.

Properties:

- at most 16 live tasks, matching the current App Manager live-instance ceiling rather than creating a larger hidden registry;
- one task is identified by ENML `ApplicationInstanceId`, signed `ApplicationIdentity`, exact live `PeerIdentity` and generation-scoped application root `SurfaceId`;
- no native PID, executable path, Linux window handle or vendor task identifier becomes shell ABI;
- publication is event-driven by trusted lifecycle/display integration; there is no task scanner, process poller or timer;
- a compositor restart may update the generation-scoped root surface for the **same exact** application instance/owner without manufacturing a new task;
- an instance cannot be rebound to a different application/process identity;
- one live exact process/root surface cannot appear as multiple shell tasks;
- activation/MRU serials are shell-owned; publishers cannot inject recency ordering;
- removing the active task returns to home instead of silently promoting another process;
- overview preserves the last active instance as navigation context but does not itself grant compositor foreground authority;
- every state mutation is revisioned with explicit exhaustion behavior.

The initial views are semantic shell state only:

- `home`
- `application`
- `overview`

These names do not prescribe a copied visual layout. Later M4 slices will render an original ENML phone shell through the existing semantic UI/material system.

## Authority split

`ShellTaskModel::activate()` records trusted shell **intent** only. It deliberately does not call the compositor directly.

A later authenticated shell authority layer will bind:

1. exact App Manager live-instance state;
2. exact application root surface ownership;
3. trusted shell principal identity;
4. compositor `activate_application()` authority;
5. system-chrome surfaces and trusted overlay ordering.

Separating semantic task state from the compositor commit keeps lifecycle/presentation races explicit and testable. Ordinary applications will not receive a public operation for globally activating themselves or mutating the shell task registry.

## Resource/power rule

The shell must become quiet when nothing changes. Task publication/removal, navigation actions, compositor frame opportunities and explicit lifecycle changes are sufficient triggers. M4.0 does not add:

- a recent-app polling thread;
- a process scanner;
- a permanent animation timer;
- a background thumbnail refresher;
- an unbounded task/event queue.

Future previews/thumbnails must be explicitly bounded, capture-policy aware and demand-driven.

## Security rule

Shell presentation is trusted system UI only when backed by shell/compositor authority. An application drawing pixels that resemble home/overview/system chrome does not become shell UI.

The existing M3 trusted-presentation rule continues to apply: technical authority comes from principal/surface/compositor state, not appearance alone.

## Validation

M4 shell work has its own GCC, Clang, ASan/UBSan and native-AArch64 matrix. The first gate tests:

- exact task publication and idempotence;
- compositor-generation root replacement for the same exact owner;
- identity/root-surface conflict rejection;
- shell-owned activation recency;
- home/application/overview transitions;
- active-task removal behavior;
- fixed-capacity failure.

## Next M4.0 slices

After the task model is green, continue in this order unless repository evidence exposes a lower-level blocker:

1. authenticated shell↔App Manager lifecycle snapshot/event boundary;
2. authenticated shell compositor authority for exact application activation;
3. compositor-owned system-chrome surface lifecycle for the shell;
4. semantic home/overview UI model using M3.2 UI tokens and accessibility;
5. bounded, capture-policy-aware task preview strategy;
6. compositor-deadline-aware transitions with reduced-motion/economy fallbacks;
7. trusted input/navigation integration without raw hardware APIs;
8. literal framebuffer/render captures and usability/accessibility refinement of the original ENML shell language.

M4.0 does not pull telephony, DRM/KMS/GPU hardware enablement, verified boot, recovery/update or full power management into the shell merely to make the milestone appear broader. Those remain separate platform tracks.
