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

## Leak detection

Leak detection is on (`ASAN_OPTIONS=detect_leaks=1`) in both the nightly runs
and the per-PR smoke job. A leak in a parser reachable from untrusted input is a
denial-of-service surface, not untidiness, and a fuzzing setup that cannot see
one is missing the finding it is best placed to make.

It was previously off across the board, which also meant the two most recently
added targets were built but never executed — a harness could rot without
anything noticing. `run-fuzz-smoke.sh` now lists every target in one place, so
adding a fuzz target and forgetting to smoke it is a single omission rather than
two, and it reports leaks and sanitizer findings as workflow annotations rather
than leaving them in a log that needs repository permissions to read.

## Next

- Triage allocation behavior and enable leak detection.
- [done] `KRG1` durable key registry is fuzzed. It is driven through the
  filesystem rather than a byte span: `load_snapshot` interleaves decoding
  with `read_exact` on the state directory descriptor, so there is no seam to
  hand a buffer. Separating decode from I/O would have meant modifying a
  durable key-state substrate `AGENTS.md` guards, purely for testability -
  the wrong trade when a slower harness reaches the same code. The staging
  directory is created once rather than per execution, so the cost is one
  write and one open per input.
