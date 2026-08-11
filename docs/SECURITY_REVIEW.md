# Security review against the references

A checkpoint rather than a milestone: what the references demand, what the tree
actually does, and where the two do not yet meet. Written to be falsifiable -
every "holds" below names the code that makes it hold, and every gap is stated
as a gap rather than as future work that sounds like a decision.

## Holds

**Fail-closed by construction.** Every wire format in the tree rejects unknown
discriminants rather than defaulting them, rejects nonzero reserved fields,
requires declared lengths to match real lengths, and refuses trailing bytes. The
default value of every security-bearing type is the one that confers nothing: a
`BootStateV1` is unverified, a `DeviceAccessPolicyV1` grants no authority. A
caller who forgets to parse, or parses and ignores the error, gets the safe
answer rather than an uninitialised one.

**Preconditions trap instead of continuing.** `os::core::invariant_violated`
emits an illegal instruction rather than calling `abort()`, so it cannot be
intercepted by a signal handler, and it is not compiled out in release builds
the way `assert` is.

**Nonce misuse is structurally prevented.** The AEAD nonce is provider-owned:
`seal()` takes it by non-const reference as an output, and no caller-supplied or
caller-influenced value can reach the cipher. This is enforced by test and
recorded as an invariant in `AGENTS.md`, because the alternative is a documented
real-world failure - a keystore that let the caller choose the IV, turning
AES-GCM into a keystream-reuse oracle.

**Authentication tags are verified in constant time.** Tag checking is
`EVP_DecryptFinal_ex`, not a byte comparison in our code, and the working copy
of the tag is wiped with `OPENSSL_cleanse` afterwards.

**No secret-indexed lookup tables.** The cache-attack literature's target is the
table-driven AES construction; recovering a full key from an ARM phone by
Prime+Probe needs no privileges at all. ENML has no cipher implementation of its
own - the only AES is the OpenSSL-backed provider used for host and CI builds -
so the attack has nothing in this tree to aim at today. The prohibition on
adding one is now recorded in `AGENTS.md`.

**Capture is an allow-list.** Surface capture defaults to denied and is granted
by enumerating the roles that may be captured, rather than by naming the one
role that may not. A deny-list silently grants to every role added later.

**Confinement is not confused with location.** A device access policy that
places a driver outside the kernel while its device can master the bus without
an IOMMU is rejected, because that isolation claim is one the platform cannot
back.

**Every parser of untrusted or durable input is fuzzed** - IPC wire, RPC errors,
OSIDL, package manifests, storage paths, boot state, the durable key registry,
and device access policy - with leak detection on, in both the per-PR smoke set
and the nightly.

## Fixed by this review

**No constant-time comparison primitive existed.** Nothing in the tree compared
secrets with `==` today, but nothing prevented the next person from doing it
either, and the naive version looks correct in review. `constant_time_equal` and
`secure_zero` now exist in `os::core`, with the rule in `AGENTS.md`.

**No binary hardening.** The build had an excellent warning set - `-Wconversion`,
`-Wsign-conversion`, `-Werror` - and not one runtime mitigation: no stack
protector, no RELRO, no non-executable stack, no position independence, no
standard-library assertions. Warnings are compile-time correctness and do
nothing to an attacker who finds a bug anyway. All of these are now enabled,
feature-tested per compiler and architecture, and carried by the target every
module already links so no module can be missed.

## Gaps

**`AeadTag` and `AeadNonce` expose public `std::array` members**, so
`a.bytes == b.bytes` still compiles and is still variable-time. The written rule
is the only control. Closing it means wrapping the storage so the naive
comparison cannot be expressed.

**Secrets are not wiped on the paths ENML owns.** `OPENSSL_cleanse` is used
inside the OpenSSL provider, which is explicitly a test fixture. `secure_zero`
now exists; the audit of which buffers should call it has not been done.

**`_FORTIFY_SOURCE` is off in Debug builds**, because it needs the optimiser to
compute object sizes and emits a warning otherwise, which `-Werror` would make
fatal. Most CI configurations are Debug. This is a real reduction in what the
gates prove, not a technicality.

**There is no time protection, and constant-time comparison is not it.** The
cache-locking work in the references is explicit that timing attacks exploit
*resource sharing*, and that the two established defences are constant-time code
and resource isolation - the first costing wasted work and depending on
micro-architectural behaviour the ISA does not promise, the second costing
either performance or silicon. It also names three leakage sources found on a
real core: cache accesses, unaligned data requests, and division and modulo,
which are not constant-time instructions on many implementations.

The consequence for ENML is that time protection is an *operating system*
responsibility requiring platform cooperation, not a property individual
functions can have. The reference design's OS is responsible for granting
partitioning permission to the processes that need it, handling failed
acquisitions, and - the part that is a security bug rather than a feature -
reclaiming partitions held by processes that were killed. ENML's supervisor
already owns exactly that shape of problem for identities and descriptors, and
owns none of it for micro-architectural resources.

`constant_time_equal` is a point fix for one class of comparison. It is not
time protection and must not be described as such. Representing what
partitioning a platform provides belongs in the capability vocabulary alongside
the boot roots of trust and device DMA confinement, and does not exist yet.

**No side-channel testing of any kind.** Constant-timeness rests on the shape of
the implementation and its optimisation barrier. The unit tests establish
correctness only; timing cannot be asserted meaningfully on a shared runner, and
a flaky timing gate would be worse than none.

**The sandbox profile is not proven minimal.** Services run under seccomp and
Landlock with a stated policy, but nothing verifies that the allowed syscall set
is the smallest that works, and a policy that is merely smaller than the default
is not a confinement argument.

**Nothing has run on real hardware.** Every property above is established on
emulated or host builds. The platform capability set is designed to represent
what real hardware provides; no real hardware has yet told it anything.

## Not applicable, and why

Several reference clusters describe attacks on structures ENML does not have:
type confusion in a scripting runtime, and the marshalling-annotation class of
driver bug. The second is a decision rather than luck - the device boundary uses
OSIDL's generated, bounded, typed formats specifically so that a hand-written
annotation cannot be wrong. Attacks that assume a permissive default (implicit
capture, ambient authority, default-open policy) do not apply because the
corresponding defaults are closed, which is the property to keep rather than a
result to record once.
