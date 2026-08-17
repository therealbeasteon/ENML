# M7.16 — the `unmap` call

**Status: decided here, implemented in the diff that carries this document.**
The last of the declared-and-unbuilt memory calls.

`docs/M7_16_MAP.md` deliberately left this out and said why: *"unmapping raises a
question mapping does not, namely whether the caller may unmap a mapping it did
not establish. That question deserves its own decision rather than being
answered by whichever way this call happened to be written."* This is that
decision.

## The question that makes it dangerous

**`unmap` followed by `map` is `replace`.**

M7.16c established that `map` cannot rewrite a live mapping — it uses
`aarch64_back_user_page`, which only fills an *absent* translation, so a caller
holding a space cannot restamp the permissions of memory a process is already
executing from. That protection is worth exactly as much as `unmap` lets it be
worth: a caller that can unmap a page and then map it again has restamped it in
two steps.

And it is worse than permissions. Under `docs/M7_16_ENTRY_FROM_REGION.md` a
thread begins at the base of its space's executable region. A principal that
could unmap that region and map a different one at the same address would have
chosen what runs at the entry of a program it did not write — which is M7.12's
attack arriving through the back door, after the front door was closed.

## Decision: three arguments, and the third is the backing capability

| Register | Argument |
| --- | --- |
| x0 | the address space, as a capability |
| x1 | the virtual address |
| x2 | the memory that is mapped there, as a capability over a grant |

The declared `argument_count` of 3 turns out to be right, which is worth stating
because the last time an encoder was written against this table the count was
wrong and had been since before the kernel existed (`receive` said 2 and its
decoder took 3). It was checked rather than assumed — the encoder consults
`describe_call`, so a wrong count is a failing test rather than a silent
mismatch.

**The extent comes from the grant, exactly as `map`'s does.** `machine_unmap`
needs a length, and the two places it could come from are the caller and the
authority. Taking it from the caller would reintroduce the reconciling check
`map` deleted. So x2 is the same backing capability the mapping was established
with, and its grant states the extent.

**That third argument is not bookkeeping — it is the second half of the
authority.** Mapping required holding *both* the space and the backing.
Unmapping requires the same two, and the symmetry is the point: **a principal
may undo only what it could have done.** A pager holds every space it services
and holds none of their backing, so it cannot unmap a process's memory even
though it can furnish memory to that process. Holding a space is permission to
*give*, never permission to *take*.

The required rights are therefore `map`'s: `address_space_right_hold` on the
space and `memory_right_map` on the backing. No new right, for the reason M7.12
gave when it declined to split `hold`: the distinction being drawn is between
furnishing memory and running code, and this is not within that.

## Decision: the executable region is never unmappable

Even by a caller holding both capabilities.

Once a space has an executable region, that region is fixed for the space's
whole life. `unmap` naming it is refused with a distinct code. The reasons
compose rather than repeat:

- It is what stops `unmap` + `map` from being `replace` for the one region where
  replacement is a code-substitution attack.
- The entry is derived from that region's base. A space whose executable region
  can disappear is a space whose entry can disappear, and a thread already
  running in it would be executing memory the space no longer claims.
- "One executable region per space" (`M7_16_ENTRY_FROM_REGION.md`) becomes a
  statement about the space's whole lifetime rather than about a moment in it.
  A rule that holds only until someone unmaps is not the rule it appears to be.

**The way to reclaim that memory is to destroy the space**, which already exists,
already zeroes what it releases, and already retires the epoch so stale
references fail closed. That is the honest answer: an address space is not a
mutable container that code can be swapped in and out of; it is the thing a
program *is*, and it ends when the program does.

Cost, stated: a program cannot unload its own text to save memory, and a loader
cannot rebuild a space in place after a failed load — it must destroy and start
again. Both are real, both are cheap, and neither is worth the attack they would
buy back.

## What this is not

- **Not a general revoke.** Revoking the *capability* over a grant is
  `capability_revoke`, which is a different call about a different object and is
  still unimplemented. Unmapping removes a translation; it does not withdraw
  anyone's authority over the memory.
- **Not teardown.** `aarch64_release_address_space` unmaps everything a space
  holds, including its executable region, and is correct to: the space is
  ceasing to exist, so there is no entry left to protect and no thread left to
  protect it from.

## Exit criteria

- `decode_unmap_syscall` refuses a zero capability on either argument and a zero
  address, each with a distinct code, proven able to fail.
- `Kernel::unmap_authorize` refuses a caller holding the space without the
  backing, the backing without the space, either without the right it needs, a
  retired space, and **the executable region**, the last with a code of its own.
- `os::abi::encode_unmap` round-trips through the kernel's own decoder, so the
  table, the encoder and the decoder must agree or the test is red.
- The M7.10 count moves by what the kernel gained, justified in the same diff.
