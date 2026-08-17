# M7.14 — The syscall ABI, and its stub library

**Status: this milestone.** `docs/M7_12_FIRST_PROGRAM.md` lists four things a
userland needs and calls the first of them — a syscall stub library — mechanical.
It is not, and this document exists because writing it found the reason.

The argument half of the ABI is real: `abi.hpp` fixes sixteen calls, each with a
declared authority and argument count, and seven of them have decoders that turn
raw register values into typed structures. **The answer half does not exist.**
`cookie_kernel_syscall_entry` writes `frame->x[0] = 1ULL` on one path and
`0ULL` on another, and those two lines are the entire return convention. That is
a convention only in the sense that one file agrees with itself.

A return convention is not something to arrive at by accretion. Every program
Cookie ever runs inherits it, it cannot be changed once anything depends on it,
and — unlike the argument half — nothing about it is forced by the hardware.

## What the calling convention is

Fixed here, for every call, forever:

| Register | In | Out |
| --- | --- | --- |
| `x0`–`x3` | arguments, per `CallDescriptor::argument_count` | results, per call |
| `x7` | **zero, written by the caller** | the outcome tag |
| `x8` | the `KernelCall` number | unchanged |

Four argument registers because `max_call_arguments` is 4 and that is a design
constraint rather than a buffer size — a call needing more is carrying a
structure, and a structure belongs in a message. `x8` because the kernel already
reads the call number there. `x4`–`x6` are unused and stay that way.

## Decision: the outcome is a tag in its own register, not a value in `x0`

The two conventions everyone else uses both encode the outcome *into* the result.

POSIX returns `-1` and puts the reason in `errno`. Linux's kernel ABI returns
the result, reserving `-4095..-1` for errors. Both reinterpret part of the value
space, and both have the same failure: the day a legitimate result lands in the
reserved range, a success is read as a failure or the reverse, silently. `errno`
adds a second: a global mutable location that survives across calls, so a caller
that checks it at the wrong moment reads a stale answer that is indistinguishable
from a fresh one.

Cookie has registers to spare and no reason to economise. **`x7` carries an
outcome tag and nothing else**, so no result value can ever be mistaken for an
outcome and no outcome for a result.

The two tags are **bitwise complements**:

```
outcome_answered = 0xC00C1EA511ED0001
outcome_refused  = 0x3FF3E15AEE12FFFE
```

Their Hamming distance is 64. No number of bit flips short of every bit turns a
refusal into an answer. Neither is `0`, and neither is all-ones — the two values
a register most plausibly holds by accident.

On refusal, `x0` carries the reason: an `os::core::Error` is `{domain: u16,
reserved: u16, code: u32}`, which is exactly 64 bits, so it crosses the boundary
without being narrowed into a number and widened back into a guess. `reserved`
must be zero and a non-zero `reserved` is refused on decode, the same rule
`.ckx` applies to its own reserved fields.

## Decision: the caller zeroes `x7`, and that is the security content

This is the part that is not bookkeeping.

An outcome tag detects a kernel that answers wrongly. It does not detect a
kernel that **does not answer at all** — a path that returns without writing
`x7`, which is what every one of the nine calls with no dispatch does today. If
`x7` held a leftover `outcome_answered` from the caller's *previous* call, that
silent path would read as a success, and the caller would proceed on a result
that was never produced.

So the stub writes `x7 = 0` before the `svc`, and **zero means the kernel did
not answer.** The property then holds against the kernel forgetting, not merely
against a frame that happens to be blank.

Cookie has been here before, in a different register. M7.12's `thread_admit`
told the *scheduler* a thread was not runnable while the *rendezvous* still said
`ready`, and the next IPC anywhere in the system put a frameless thread on a
CPU. **Saying "not runnable" to one of the two structures that decide it is not
saying it at all.** An answer is decided by two parties too. The caller's job is
to make "nobody wrote one" a state that exists, instead of one that borrows the
appearance of the last successful call.

## Decision: the stub reads the ABI table rather than restating it

Every encoder takes its argument count from `describe_call`, not from a literal.
A stub that hard-codes "send takes three" is a second copy of the ABI, and the
day the two disagree is a silent wire break: the kernel reads `x2` as a length
and the caller wrote a deadline there.

**Registers past the declared count are written zero, not left alone.** Two
reasons, and the second is the one that matters. A leftover value is a
disclosure of the caller's own state into the kernel. And it makes a kernel bug
invisible: if a dispatch ever reads an argument the descriptor says does not
exist, a zero is obviously wrong and a plausible leftover is not.

## Decision: what is not callable is written down and tested

Seven calls have decoders. Nine do not, so their argument contracts do not exist
anywhere — and inventing them from the userland side would create exactly the
second source of truth this project has refused twice before (ELF beside the
package model; seL4's separate derivation tree beside capabilities).

So the stub library implements the calls whose contract the kernel has defined,
and **names the rest in a constant that a test checks against the ABI table.**
Adding a stub without removing its call from that list fails. Adding a
seventeenth call to the ABI without deciding either way fails. The gap stays a
gap that is written down, which is the only kind that does not surprise anyone.

## Why this is host-testable, and why that was worth designing for

The library is split so that everything except the trap itself is architecture-
neutral:

- `encode_*` turns typed arguments into a register set. Pure.
- `decode_outcome` turns a register set into a `Result`. Pure.
- The `svc` lives alone, in an AArch64-only translation unit.

That split is what lets the whole contract be checked on the host, in the same
CI job as everything else, instead of only through a QEMU boot. It matters more
here than it usually would: `kernel-arm64-native` is this project's only real
verification and it costs a boot per attempt, so a contract that can be proven
without one should be.

The test does three things:

1. **Differential.** For each of the seven defined calls: encode a typed
   structure, hand the raw registers to the *kernel's own decoder*, assert
   field-for-field equality. The encoder is never checked against a second
   opinion about the layout; it is checked against the reader. This is the rule
   `build_ckx` established — a writer that validates by re-reading cannot
   disagree with its parser, because there is nothing for it to disagree with.
2. **Surface completeness.** Walk `call_at`/`kernel_call_count` and assert every
   call is either implemented or listed as not implemented, and never both.
3. **The zero case, explicitly.** A register set the kernel never wrote decodes
   as "no answer", not as success. This is the assertion the whole `x7` design
   exists for, so it is written as its own case rather than inferred.

## What this does not decide

- **Which calls the kernel dispatches.** M7.15 rewrites
  `cookie_kernel_syscall_entry` from the proof scaffold it is into a dispatcher
  driven by the ABI table. This milestone gives it a return convention to
  implement; it does not implement it.
- **How a program starts.** What is in the registers and on the stack at a
  program's first instruction is the loader's contract, and belongs with the
  loader (M7.16).
- **Anything about IPC message content.** `IpcUserExchange` already governs
  that. This is the register boundary only.

## Exit criteria

- A compiled translation unit outside `core/oskernel` encodes every defined call
  and reads a typed answer back, with no literal copy of the ABI table in it.
- The differential test passes for all seven calls that have kernel decoders.
- A register set the kernel did not write is refused, and the test says so by
  name.
- The M7.10 line count does not move. **Nothing in this milestone belongs in the
  trusted image**, and the count is where that stays checkable — the same place
  the `.ckx` parser's absence from the kernel is visible.
