# Naming

The project has three names, and they are not interchangeable. Using them
loosely is how a security claim ends up attached to the wrong thing.

| Name | What it is |
| --- | --- |
| **Cookie** | The operating system. The product. |
| **Cookie Kernel** | The microkernel Cookie runs on, defined in `docs/M7_0_KERNEL.md`. |
| **EMNL** | The security architecture - the boundaries, policies and invariants Cookie enforces. |

## Why they are separate

**Cookie** is what a user has. It boots, shows a shell, runs applications.

**Cookie Kernel** is the thing whose size is the whole security argument. It has
its own name because it has its own measure of success - how many lines have to
be trusted - and that measure must not be diluted by counting the rest of the
system. A claim about the kernel is a claim about 605-line-class code; a claim
about Cookie is a claim about an operating system.

**EMNL** is the part that is portable across both. The typed identities, the
brokered capabilities, the fail-closed wire formats, the device access policy,
the consent binding, the boot state - none of these are properties of a kernel
or of a product. They are the security architecture, and they were designed to
survive the substrate changing, which is exactly what happened when the kernel
decision was revisited. Naming them separately makes that explicit rather than
accidental.

The practical rule: if a statement would still be true after the kernel is
replaced, it is about EMNL. If it depends on how a call is dispatched or how an
address space is switched, it is about Cookie Kernel. If it is about what
someone holding the phone experiences, it is about Cookie.

## File extensions

| Extension | What it is |
| --- | --- |
| **`.ckx`** | A Cookie executable image. One program, described as *a plan for an address space to construct* rather than as bytes to load — Cookie has no load operation. |
| **`.cookie`** | An application package. What a user installs, and what `ApplicationIdentity` names. Contains one or more `.ckx` plus the manifest. |

The split matches the table above rather than cutting across it. A `.cookie` is
a **Cookie** thing — what someone holding the phone installs and sees. A `.ckx`
is closer to the **Cookie Kernel** boundary: regions, permissions, disclosure
classes and an entry, described in exactly the terms the kernel's own address
spaces use, because that is what it is a plan for.

The rule that follows: **a `.ckx` on its own is untrusted bytes.** It carries no
signature and cannot certify itself; trust comes from the `.cookie` containing
it. `docs/M7_12_CKX_FORMAT.md` has the format and the reasoning.

## Identifiers

Source identifiers have not been renamed and are not scheduled to be renamed
casually. `os::` namespaces, `emnl_*` CMake targets and existing file paths stay
as they are for now, because a mechanical rename across a tree with twelve
gates is a change whose risk is entirely typos - and it buys nothing a reader of
this table does not already have. The local pre-flight check in `AGENTS.md`
covers `oscore` and `oskernel` only, which is a small fraction of what a rename
would touch, so for this particular change CI remains effectively the only
compiler.

When identifiers are renamed it will be as its own change, verified on its own,
touching nothing else. In the meantime `emnl_` in a target name should be read
as "this project", and the security-architecture sense of EMNL is the one this
document defines.

## Repository

The repository is still named ENML. That is a spelling of the same word and
predates this document; renaming it breaks every existing clone and remote for
no benefit that a README cannot provide.
