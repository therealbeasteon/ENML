# M7.16c — the `map` call

**Status: decided here, implemented in the diff that carries this document.**
This is the first of the two calls a loader needs and the one that has been
declared longest without existing.

## Why this milestone starts here

`docs/ROADMAP.md`'s Phase 2c ends with a loader: something that executes a
`.ckx` plan against the syscall surface, so that a *second* program can be
started. M7.16b ran the first one. Writing the loader immediately would have
found what a review found first, and it is worth stating as a finding rather
than as a prerequisite discovered in passing:

**`map` and `unmap` are in the published sixteen-entry ABI table — with an
authority class, an argument count, and a comment explaining why establishing a
mapping is authority rather than convenience — and neither has a decoder, a
kernel operation, an EL0 dispatch, or a stub.** Nothing in the tree implements
either. A `.ckx` is a plan for an address space to be constructed, and the
operation that constructs one was declared and never built.

This is the third occurrence of the defect class this project keeps
rediscovering, and the first where the thing declared-and-unengaged is a whole
kernel call rather than a field or a test label. `recovery_policy_test` was
registered and never selected; `image_ckx_test` declared a label no workflow
matched; `receive`'s `argument_count` said 2 while its decoder took 3. Each was
invisible because nothing consumed the declaration. **A published ABI table is
consumed by whoever writes a program against it**, which is a consumer this
project did not have until M7.16a and now does.

## Decision: what the four arguments are

`describe_call` has said `map` takes four arguments since before the kernel
existed, and said nothing about which four. They are:

| Register | Argument |
| --- | --- |
| x0 | the address space to map into, as a capability |
| x1 | the virtual address to map it at |
| x2 | the memory to map, as a capability over a grant |
| x3 | the permissions |

The count was not chosen to fit; it fit. That is worth noticing rather than
celebrating — it is weak evidence the original intent was close to this, and no
evidence at all that this is right, so each argument is justified below on its
own.

**x0 is a capability, not an address space identifier.** The same reason
`address_space_destroy` takes one: an identifier is a number a caller can guess,
and the refusal it earns tells the caller which spaces exist. The required right
is **`address_space_right_hold`**, not a new `right_map`.

That deserves defending, because M7.12 added `address_space_right_admit` as a
separate bit on exactly this kind of question. The rule that produced it was: *a
pager holds every space it services and must not be able to run code inside
them.* Running code and furnishing memory are different authorities, and the
split is between those two — not within the second. A holder that cannot map
cannot do the one thing holding a space is *for*: `fault_supply` already
establishes a mapping in a space on the holder's behalf, and it requires no
right beyond the fault handshake. Adding `right_map` would mean a pager could
answer a fault it was asked about and not map memory it was never asked about,
which is a distinction the fault path already draws by asking, and drawing it
twice in two places is how the two come to disagree.

**x1 is where.** Nothing else about layout is the kernel's business —
`docs/M7_11_MEMORY.md` already decided that where a process's regions go is a
userland decision.

Which layer refuses a *bad* x1 is a real question and the answer is not the
decoder. The decoder refuses what is wrong about the **encoding**: a zero
address, which is what a zeroed register set produces and must never name a
mapping. Alignment and address range are refused by the **machine layer**, which
owns them — `aarch64_map_user` already enforces `Stage1Region::lower`, and
`user_stage1_virtual_address` is an AArch64 predicate about an AArch64 address
layout. Putting either in the portable decoder would put a second, weaker copy
of a rule beside the one that enforces it, which is how the two come to
disagree; the existing capability decoders say so in as many words, and the
page granule would additionally become a *third* statement of 4096 alongside the
machine layer's and `.ckx`'s. The caller pays one extra layer of call depth for
its refusal and gets a correct one.

**x2 is a capability over a `MemoryGrant`, and never a physical address.** A
caller naming physical memory directly would be asserting an authority the
physical ledger exists to check. `memory_right_map` is the required right, which
is the same right `fault_supply` requires of the backing it is handed — the same
operation being authorised, so the same right.

**x3 is the permission, and it is one of three.** The machine layer has
`read`/`read_write`/`read_execute` and nothing else, `.ckx` mirrors those three
for the same reason, and this makes a third statement of them. The three
statements are held together by `static_assert` rather than by intent, so
divergence is a build failure — which is what M7.15a did with the outcome tags
after defining them twice was caught one commit later.

## Decision: the length comes from the grant, and is not an argument

**This is the decision in the milestone, and it is the one with a real cost.**

A caller does not say how much to map. The mapping covers the grant that x2
names, entirely.

The argument for it is that a length argument is a second statement of
something the authority already says, and the two can disagree. A caller passing
`(grant, length)` can name a length longer than the grant, so a check must exist
to refuse that — and the check is the kind that is written once, tested for the
obvious case, and quietly wrong for the overflowing one. `MemoryGrant::contains`
exists today and is careful about exactly that wrap. Deriving the length deletes
the check instead of adding one.

It is also the rule the rest of the design already follows. `.ckx` computes its
construction cost from the plan rather than declaring it, "because an image
cannot lie about what it will take to build"; M7.16's link address and its
region address come from one definition; `fault_supply` names backing and lets
the kernel supply the region it already knows. **Derived-not-stored is the
security content** each time, and this is the same shape.

**The cost, stated plainly: a holder of a large grant cannot map part of it.**
There is no range-narrowing primitive today — `capability_grant` narrows
*rights*, not extents — so a caller who needs a sub-range must have been given a
grant of that extent by whoever had authority to make one. For the loader that
is the right shape anyway: a `.ckx` region is a unit of content addressed by
digest, and the backing for one region is what a pager or package layer hands
over. It is not the right shape forever, and the resolution when it is needed is
a grant-narrowing call rather than a length argument here — narrowing authority
is an operation on authority, which is where a capability system puts it.

## Decision: this authorises, it does not map

`Kernel::map_authorize` resolves and checks; the machine layer performs. That is
the split every address-space operation in this kernel already uses —
`Kernel::address_space_create` mints authority and `aarch64_create_address_space`
builds tables — and it is what keeps the portable core host-testable. A
`MapAuthorization` is the resolved answer: which space, which physical range,
where, and with what permissions.

It carries the *resolved* physical base rather than the capability, and that is
deliberate: the machine layer must not re-resolve, because a second resolution
is a second answer, and the window between them is where the capability could
have been revoked.

## What is deliberately not here

- **`unmap`.** Declared with three arguments and equally unimplemented. It is
  the next diff, not this one, and it is not symmetric with this one: unmapping
  raises a question mapping does not, namely whether the caller may unmap a
  mapping it did not establish. That question deserves its own decision rather
  than being answered by whichever way this call happened to be written.
- **EL0 dispatch.** `cookie_kernel_syscall_entry` refuses every `memory_control`
  call except `fault_supply`, by name, precisely so an unimplemented one cannot
  fall through to a wrong answer. Widening that is the diff that also carries a
  boot proof, because a dispatch with no caller is the seam-with-nothing-behind-it
  this project has criticised twice.
- **Mapping the same physical range into two spaces.** The ledger already
  refuses a `writable_executable_alias` and already records who mapped what, so
  sharing is a question the existing check answers rather than a new policy this
  call needs to state.

## Exit criteria

- `decode_map_syscall` refuses a zero capability, a zero virtual address, and a
  permission value outside the three — each with a distinct code, and each
  proven able to fail before being trusted. It refuses nothing about alignment
  or address range, which belong to the machine layer, and that absence is
  deliberate rather than missing.
- `Kernel::map_authorize` refuses a caller that does not hold the space, does
  not hold the backing, holds either without the required right, or names a
  space whose epoch has been retired.
- `os::abi::encode_map` produces a request the kernel's own decoder accepts, and
  the round trip is a test — so the table, the encoder and the decoder must
  agree or the test is red.
- The M7.10 count moves by what the kernel gained, justified in the same diff.
