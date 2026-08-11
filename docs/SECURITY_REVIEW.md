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
time protection and must not be described as such.

Partly addressed since: `os::time::PartitionLedger` (M6.2) is the OS half -
capabilities, granting, refusal and reclamation on death, with the rule that a
reservation may never consume the shared remainder. The half that actually
partitions hardware is a platform port and does not exist, so **ENML still has
no time protection**. The accounting being correct is a precondition for it,
not a substitute.

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

## Corrections

The intermittent M2 suite failure was previously diagnosed as a one-second
teardown bound in two broker helpers, and those bounds were widened on that
basis. A later occurrence, with annotations that name the test, shows the
diagnosis was wrong: the failing test is
`app_manager_runtime_service_session_test` - neither of the two that were
widened, and one that already carried a thirty-second bound - and it **aborts
in under a second** rather than timing out. An abort that fast is a failed
assertion, not a scheduling delay.

The widened bounds are harmless and remain, but they did not fix this and were
never shown to.

The log tail added for that purpose then caught it:

    app_manager_runtime_service_session_test.cpp:446:
    Assertion `manager.uninstall_application(application)' failed.

`uninstall_application` collapses the durable no-active-generation commit,
profile revocation against Storage and Keys, identity release, child teardown
and a final `maintain()` into a single `first_error`, and the test discarded it.
It now prints the domain and code before asserting.

A later occurrence, in a different job, produced the diagnosis outright:

    m2.10 child failure stage=key-marker exit=32 domain=2 code=8
    Assertion `wait_for_file(manager, data_fd,
    "m2-10-storage-reacquired.bin")' failed

Domain 2 code 8 is `ipc::peer_died`. The child fixture reacquires the **key**
service after its restart, then writes its key-marker through the **storage**
capability it acquired before any restart - and this test restarts storage too.
The loop immediately below that write tolerates `peer_died` on the same handle,
because observing the old storage capability die is its purpose. The marker
write does not, so if the storage restart lands one statement earlier than the
fixture assumes, the child exits 32 and the parent then waits the full thirty
seconds for a marker that will never be written.

That accounts for both observed shapes: the 0.8-second abort and the
30-second one are the same race caught at different points.

Fixed. The marker write now tolerates `peer_died`, records that the old storage
capability has already died - which is what the heartbeat loop was waiting to
establish, so that loop is skipped rather than spinning to its bound - and
writes the marker through the reacquired root instead. The marker still means
what it meant, and the parent still receives it.

This is the third diagnosis of this failure. The first two were wrong because
they were made without evidence: a bare `assert` on a bool, and a summary line
naming only an exit code. What made the difference was carrying the failing
test's own output into the annotation, which cost far less than the guesses
did.

