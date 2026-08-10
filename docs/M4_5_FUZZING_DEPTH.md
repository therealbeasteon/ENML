# M4.5 Fuzzing depth

## Implementation authority

**References teach principles. ENML determines implementation. External systems are not the design specification.**

The supplied hardening and mobile-security material reinforces attack-surface
reduction and explicit threat modeling for untrusted input. It does not
prescribe ENML's fuzzing topology, corpus layout or CI cadence.

## The gap this closes

ENML already had four libFuzzer harnesses covering its untrusted-input parsers:
the IPC wire decoder, the RPC error envelope, the OSIDL compiler and the package
manifest analyzer. Each is built with ASan and UBSan. The hard part — writing
harnesses for the right surfaces — was already done.

What was missing was actually running them. The `fuzz-smoke` job in `ci.yml`
executes 1000 cases per target (5000 for the manifest analyzer), which takes
about a second. That is a build gate: it proves the harnesses still compile and
do not crash on trivial input. It is not a search.

Worse, an unseeded run barely reaches the code under test.
`decode_wire_header_v1` rejects on the first four bytes unless they are exactly
`OSIP`. A byte-level mutator finds that at a rate of one in 2^32, so a
from-scratch `ipc_decoder_fuzz` run never reaches flag validation, payload
bounds, reserved-field checks or UTF-8 handling at all. Nearly the whole budget
was spent failing the magic check.

## What is added

**Seed corpora.** `fuzz/corpus/generate_seeds.py` materializes structurally
valid starting inputs: one wire packet per primary message class and modifier
combination, one RPC error envelope per `ErrorDomain` plus the reject
boundaries, and package manifests that clear magic and version. OSIDL seeds are
the checked-in `.osidl` interface definitions themselves, which are the only
guaranteed-valid OSIDL that exists.

Seeds are generated rather than committed as binary blobs. The structure of
every seed stays reviewable as source — these bytes encode ENML's frozen wire
contracts, and an opaque blob in the tree cannot be audited against them.

**Dictionaries.** `fuzz/dict/ipc_wire.dict` and `fuzz/dict/osidl.dict` give the
mutator the vocabulary it cannot assemble by chance: the `OSIP` magic, header
size and version constants, each `WireFlag` value in its little-endian wire
position, payload-size and handle-count values that straddle the bounds, the
UTF-8 shapes the validator must reject (overlong, surrogate, above U+10FFFF),
and the OSIDL keyword set.

**A nightly workflow.** `fuzz-nightly.yml` runs each target for ten minutes with
its corpus and dictionary, then minimizes the corpus and caches it. Coverage
accumulates across nights instead of restarting from nothing. Crashing inputs
are uploaded as artifacts so a reproducer survives the runner.

The per-PR smoke job is deliberately left unchanged. PR feedback must stay fast;
these are complementary signals, not a replacement.

## Invariants

- The nightly workflow validates itself. A schedule-only workflow is never
  exercised before merge, so it also runs a short version on any pull request
  touching `fuzz/**` or the workflow file. A broken harness, dictionary or seed
  generator is caught by the PR that breaks it, not by a silent 3am failure.
- PR validation runs never write the shared corpus cache. A 60-second branch run
  must not overwrite accumulated nightly coverage.
- `fail-fast` is off. One target finding a crash must not cancel the others.
- Seed generation constants are mirrored from `constants.hpp`, `wire.hpp` and
  `analyzer.hpp` and must be updated with them. A seed that no longer matches
  the wire format silently degrades back to unseeded fuzzing.

## Known limitation

Leak detection stays off (`ASAN_OPTIONS=detect_leaks=0`), matching the existing
smoke job. Turning it on requires triaging the OSIDL compiler's known allocation
behavior first; enabling it blind would make the nightly signal unreliable from
day one. This is worth doing as a separate change — a leak in a parser reachable
from untrusted input is a denial-of-service surface, not merely untidy.

## Next

- Triage allocation behavior and enable leak detection.
- Fuzz the `KRG1` key registry decoder. This is blocked on structure, not
  effort: `PersistentKeyRegistry::load_snapshot` interleaves decoding with
  `read_exact` calls on the state directory descriptor, so there is no
  byte-span seam to drive. Reaching it needs either a decode/IO separation in
  a substrate `AGENTS.md` guards explicitly, or a filesystem-backed harness
  that stages each input through a temporary directory and pays syscalls per
  execution. Both are legitimate; the choice should be a reviewed decision
  rather than a refactor bolted on for fuzzability.

## Covered targets

`ipc_decoder_fuzz`, `rpc_error_fuzz`, `osidlc_compiler_fuzz`,
`package_manifest_fuzz`, `storage_relative_path_fuzz`.

`storage_relative_path_fuzz` targets the private-storage confinement boundary.
Beyond crashes it asserts that parsing is idempotent: if `parse()` accepts input
whose `view()` no longer re-parses, or re-parses to different bytes, confinement
has been broken without any memory-safety error occurring. That divergence would
otherwise pass silently.
- Consider a structure-aware mutator for the wire header once the transport ABI
  is frozen, so mutation preserves header validity and spends its budget on
  payload semantics.
