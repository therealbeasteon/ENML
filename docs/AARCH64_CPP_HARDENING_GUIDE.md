# Cookie Kernel C++20 / AArch64 Hardening Guide

Status: ACTIVE ARCHITECTURE CONTRACT. This guide may influence current review and tests, but it does not unlock roadmap milestones that are still gated.

## Goal

Use each language only where it improves correctness and auditability. Cookie Kernel is primarily freestanding C++20 because its security model depends on typed state machines and explicit object lifetimes. AArch64 assembly is intentionally small and restricted to architectural boundaries where the compiler cannot directly express the required machine transition.

The objective is not "more assembly" or "more C++". The objective is the smallest auditable trusted implementation with machine-level behavior that can be proven against the Arm architecture and software invariants that can be proven against typed interfaces/tests.

## C++ owns policy and persistent invariants

Prefer C++ for:

- capability creation, attenuation, derivation and revocation;
- `ExecutionAuthority` and address-space generation semantics;
- endpoint/IPC transaction state;
- scheduler and rendezvous state machines;
- page-table construction and mapping manifests;
- process translation bindings and sealed-root lifecycle;
- memory-object ownership/mapping policy;
- boot/update policy decisions above primitive machine entry;
- bounds/overflow validation and error propagation;
- driver protocol state where direct instruction access is not required.

Why: strong types, explicit constructors/results, constexpr validation and small fixed-size containers make invalid security states harder to represent than integer/register-oriented interfaces.

## Assembly owns architectural transitions

Assembly is appropriate for:

- reset/boot entry before a valid C++ execution environment exists;
- exception vector entry/exit and exact register-frame capture;
- EL1↔EL0 transition sequences;
- low-level context-switch register save/restore;
- controlled user-memory primitives where fault windows/instruction ordering are architectural;
- barriers/TLB/cache/system-register sequences that must match the Arm architecture exactly.

Assembly must not contain capability policy, endpoint selection, authorization rules, scheduler policy or cryptographic protocol logic.

## Assembly interface rules

Every assembly entry point should have one C++ declaration describing its ABI and ownership expectations.

Required properties:

1. Explicit `extern "C"` linkage where called across the C++/assembly boundary.
2. Namespace qualification is explicit at C++ call sites even though C linkage fixes the linker symbol; this prevents declaration/scope mistakes.
3. Argument/result registers are documented next to the declaration or `.S` implementation.
4. Callee-saved vs caller-saved ownership follows AAPCS64 unless an entry is a raw exception/reset vector; deviations must be documented.
5. Stack alignment is maintained at 16 bytes at public AAPCS64 boundaries.
6. Exception entry saves all state needed to resume the interrupted security context before C++ can mutate scheduler/process state.
7. Assembly never trusts EL0-supplied identity. C++ derives identity from the live kernel binding after entry.
8. System-register writes use the architecturally required `DSB`/`ISB` ordering; barriers are part of correctness, not performance decoration.
9. Assembly routines are tiny enough to inspect completely and receive dedicated disassembly/boot tests.

## Freestanding C++ rules

The standalone kernel must remain independent of the hosted C++ runtime:

- no exceptions;
- no RTTI;
- no thread-safe static initialization dependency;
- no implicit dynamic allocation in core kernel paths unless Cookie later defines and audits its allocator contract;
- no iostreams/filesystem/host threading/runtime facilities;
- no Linux/POSIX headers in the standalone target;
- no hidden reliance on TLS, environment, locale or process startup runtime.

Use fixed-capacity arrays/tables for early kernel subsystems where exhaustion can be reported explicitly and bounded memory is a security property.

## Type-system hardening

Security-relevant identifiers should not collapse into one integer namespace.

Prefer distinct types for:

- capability ID vs object ID;
- endpoint slot/generation;
- transaction ID;
- address-space slot/generation;
- hardware ASID;
- physical vs virtual address wrapper where practical;
- trusted `ExecutionAuthority` vs reusable `ThreadId`.

A hardware tag such as ASID is never promoted into software authority merely because both fit in an integer.

`Result<T>` conversions should remain explicit. Tests must adapt their assertion helpers rather than weakening the production type. This prevents security-sensitive error-returning functions from being silently mixed into arithmetic or overload resolution.

## Lifetime and reuse discipline

Reusable slots/IDs require a generation or a proof that all dependent state is destroyed before reuse.

For kernel objects that outlive a single instruction:

- creation defines an owner/authority and generation;
- delegation is explicit;
- retirement invalidates new lookup first;
- in-flight references are drained/cancelled;
- machine-visible reuse (ASID, memory, endpoint slot) happens only after required retirement barriers;
- old software authority never becomes valid merely because the numeric slot/tag reappears.

Delayed reuse/quarantine is preferred for security-sensitive allocators where memory overhead is acceptable, particularly for objects whose stale references would become exploitable use-after-free primitives.

## Integer, bounds and pointer rules

- All user-controlled lengths/offsets are range checked before addition/multiplication.
- Overflow checks occur before align-up or end-address calculations.
- Raw user pointers are not dereferenced in general C++; they become bounded access tickets tied to a live translation generation.
- Physical addresses and MMIO ranges are validated for overflow and containment before mapping.
- Signed/unsigned conversions at ABI boundaries are explicit.
- Narrowing conversions are deliberate and checked against protocol limits.
- Zero-length behavior is explicitly specified for every buffer/range API.

Compiler warnings for conversion, sign conversion, shadowing, format use and pedantic language behavior remain errors in CI where supported.

## Memory permissions

Default mapping policy:

- kernel text: read + execute, not writable;
- kernel rodata: read-only, non-executable;
- kernel data/heap/stacks: read + write, non-executable;
- user code: read + execute after loading/verification; avoid writable+executable mappings;
- user data/stacks: read + write, non-executable;
- MMIO: device memory attributes, non-executable;
- guard regions: unmapped.

Any JIT-like executable generation is a future exceptional feature requiring a separate W^X transition protocol; it must not normalize RWX mappings.

## AArch64 hardening opportunities

Treat these as target-capability-dependent mitigations, not substitutes for correctness:

### Branch protection

Where supported by compiler/toolchain/hardware, evaluate BTI and pointer authentication for suitable kernel/user control-flow boundaries. CI must verify emitted instructions/properties rather than assuming a flag was honored.

### PAN / privileged user access

Kernel should not have ambient access to user mappings. User copies occur through narrowly scoped controlled primitives and are revalidated against the current process translation generation.

### WXN / execute-never

Use architectural execute-never and writable-never-executable policy so writable data cannot become an instruction source by default.

### Memory tagging

For hardware that supports MTE, evaluate tagged allocators for userspace services first and later kernel heaps if measurable benefit exceeds complexity. Correctness must not require MTE to catch stale references.

### Speculation boundaries

Use architectural recommendations for speculation/barrier behavior at privilege and sensitive mapping transitions. Avoid scattering speculative barriers without a documented threat and architectural reason.

## Compiler/toolchain policy

Maintain at least GCC and Clang kernel builds because different front ends expose different undefined behavior, warning and ABI issues. Native ARM64 is mandatory because a cross/host-only build cannot prove assembly integration or target ABI behavior.

CI should include:

- GCC warnings-as-errors host kernel tests;
- Clang warnings-as-errors host kernel tests;
- sanitizer builds for pure C++ portions that can run hosted;
- native ARM64 compilation of standalone image;
- QEMU boot proof for architecture paths;
- disassembly/symbol checks for critical assembly boundaries when practical;
- Linux-coupling scan for standalone/production kernel code.

Do not weaken language safety merely to make one compiler happy. Fix ambiguous/narrowing/explicit-conversion call sites instead.

## Undefined-behavior policy

Kernel C++ must not rely on signed overflow, type-punning alias violations, uninitialized reads, invalid object lifetime tricks or races that happen to work on one compiler optimization level.

When device/register access requires volatile semantics, isolate it in a small machine/driver primitive. `volatile` is not a concurrency primitive and does not replace atomic ordering or architecture barriers.

## Concurrency preparation

Before M7.10 SMP:

- document which tables are single-core by construction;
- do not sprinkle atomics into structures without ownership/locking design;
- distinguish interrupt exclusion, preemption exclusion and multi-core mutual exclusion;
- define memory-order requirements around scheduler/runqueue, capability teardown, TLB shootdown and IPC state before making them concurrently mutable.

M7.10 should introduce concurrency intentionally, not convert existing fields to atomics mechanically.

## Cryptography boundary

Assembly optimization may eventually be used inside a vetted cryptographic provider, but Cookie security protocols must depend on standardized, reviewed primitives and provider APIs. Kernel architecture assembly must not grow ad-hoc AES/SHA/signature implementations as part of unrelated machine code.

Hardware crypto acceleration is an implementation detail below a primitive/provider interface, with known-answer tests ensuring equivalence to the portable implementation.

## Review checklist for every assembly addition

- Could this be expressed safely in C++ instead?
- Is direct register/instruction control actually required?
- Is the full register clobber/save contract documented?
- Are privilege level and interrupt state explicit?
- Are memory barriers architecturally sufficient and minimal?
- Can user-controlled register contents affect trusted identity or addresses before validation?
- Is there a C++ unit/contract test plus an ARM64 execution/boot test?
- Does the routine preserve stack alignment and required registers?
- Does it avoid policy decisions?

If any answer is unclear, the assembly change is not ready to merge.
