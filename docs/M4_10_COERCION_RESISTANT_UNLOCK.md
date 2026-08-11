# M4.10 - Coercion-resistant unlock

`docs/ACHIEVEMENTS.md` has carried this as an unclaimed gap with a warning
attached to it: a lock screen needs "a coercion-resistance design that is not a
naive second 'panic PIN' - the supplied duress reference shows why simple
two-password schemes fail under repeated coercion." This is that design, and the
warning turns out to be the most useful thing in the reference.

## What the reference settles

The supplied panic-password work is unusually direct about what does not work.

**Two passwords are defeated by asking twice.** Give the user a regular password
and one panic password, and an attacker who knows the scheme - which he does,
by Kerckhoffs' principle - simply demands a second authentication with a
different password. The user has only two. She must surrender the real one. The
paper calls this the **iteration principle** and states plainly that the
best-known panic password scheme "is very easily defeated" by it.

**Disclosure is defeated by choosing for her.** Under the
**forced-randomization principle**, the attacker makes the user write down every
password she knows and picks which to use himself. Against two passwords this
gets him what he wants half the time.

**And the obvious repair is a trap.** Locking the device when the panic password
arrives lets the attacker *screen*: he demands a credential that does not lock
the device, threatens retaliation if it does, and thereby learns which is which.
The paper's conclusion is the single most important sentence for this design -
**"the event that locks the account must be invariant to the type of the
passwords being entered."**

Its answer, 2P-lock, locks when two *different* credentials are used inside a
window, whichever came first, and holds the lock for longer than the window. And
against a persistent attacker it goes further: no fixed pair is enough, because
he can outlast any finite list, so the panic family has to be arbitrarily large -
while staying far enough from the *invalid* space that a mistyped real password
is an error rather than a catastrophe.

## Where a phone breaks that model

The reference's threat model contains a line that does not survive contact with
a phone: Bob - the verifier - is trusted, elsewhere, and not in collusion with
the attacker. Every scheme in it can therefore take an **unobserved reaction**: a
silent alarm, a spoiled ballot, a flag in a database the attacker cannot see.

A phone has no such party. The verifier is the device, and the device is in the
attacker's hand. Three things follow, and they are what ENML has to answer for
itself.

**There is no unobserved channel, so the reaction must be local and
irreversible.** A silent alarm needs a network the attacker removes by holding
the power button, pulling the SIM, or standing in a basement. Any design that
depends on a message reaching somebody fails precisely when it is needed. What a
phone *can* do unobserved is destroy a key - and ENML already has the structure
for it, because M2.7 built system → profile → application protection scopes with
strictly downward-only derivation. Destroying the wrapping key of a scope makes
everything beneath it unrecoverable in the time it takes to overwrite one key.
That is the panic reaction, and it is genuinely unobserved: the attacker watching
the screen sees nothing at all.

**The observable response must be an ordinary unlock.** Not a refusal, not a wipe
animation, not a device that suddenly feels different. The duress credential
opens the phone onto what survives. The attacker got what he demanded and has no
way to tell it is not what he wanted.

**Duration is a channel, because the attacker is holding the stopwatch.**
Destroying a key takes longer than not destroying one. Balancing the branches by
inspection is the kind of promise that is true when written and false after the
next change, so the authority instead states a single deadline before which no
result may be shown, identical for every outcome. One rule to verify beats two
paths to keep equal.

## The design

**Duress is a credential class, not a second PIN.** The authority is handed a
classification - nominal, duress or invalid - and an opaque tag. It never sees a
credential, which is what lets it be tested and fuzzed with no secret material
anywhere near it, and what keeps the comparison in one place where it can be made
constant-time.

**The iteration rule reads a tag and a time, and nothing else.** Two different
credentials of the user's own inside the window lock the device, whichever came
first. The class is not merely ignored - it is *not stored*, so the screening bug
the reference warns about cannot be reintroduced by a later edit. That is the
difference between a rule that is currently correct and one that is structurally
correct.

**The lock outlasts the window, and it is a `static_assert`.** If it did not, the
owner would return after a lockout, enter the credential she normally uses, still
be inside the window against the attacker's credential, and lock herself out
again - forever. The reference states `t2 > t1` as a requirement; here it is a
build error.

**Destruction happens first, and is not conditional on being granted.** The
attacker can cut power at any instant, so a destruction that has not happened yet
is one that did not happen. And if the attacker demands the real credential first
and the duress one second, the iteration rule will refuse that second attempt -
so if destruction waited on a grant, the user would have spent her one signal on
nothing. It fires on classification, not on outcome.

**There is no `granted_under_duress`.** A duress unlock and a nominal unlock
return the same disposition, the same release time, and the same
accepting-again time. The one-shot destruction directive is the only difference
and it is consumed before access is released. Past that point nothing in the
system knows which kind of unlock happened, so no code downstream can be made to
leak it - by a bug, by a log line, or by an analytics counter somebody adds in
two years.

**Guessing and iteration are counted separately.** An unrecognised credential
feeds an escalating backoff and is never written into the iteration history -
otherwise an attacker could arm the lock by typing nonsense and then screen
against it. A recognised credential clears the guess count.

**Nothing is evaluated during a lockout.** Not classified against, not counted,
not recorded. A lockout an attacker can grind against is not a lockout.

## Threat model

**The attacker who asks twice.** Defeated. The second, different credential locks
the device, and the lock is indistinguishable in every observable - disposition,
timing, and when the device will accept again - from what would have happened had
the credentials been given in the other order.

**The attacker who asks for the list and chooses himself.** Mitigated, not
solved, and the mitigation is not in this file: the duress family must be large
and rule-derived, so that there is no finite list to disclose. The reference is
explicit that this is the only answer to a persistent attacker, and equally
explicit that the rule has to be one a frightened person can apply correctly.

**The attacker who screens.** No mechanism here distinguishes credential classes
in anything he can observe. The lock is triggered by difference, not by type; the
timing envelope is uniform; the success value is shared.

**The attacker who takes the phone away to examine later.** He finds a device
whose protected domain is cryptographically gone. That is the point.

**The owner who fat-fingers her PIN.** Lands in *invalid* - an error message and a
small delay - provided the classifier keeps the duress and invalid spaces well
mixed. That obligation is stated in the header because this file cannot enforce
it, and getting it wrong means a slip of the finger irreversibly destroys
everything the user owns. It is the single most dangerous requirement in the
design.

## What is not claimed

**Forensic indistinguishability afterwards.** ENML claims the attacker cannot
tell *at the moment of the coerced unlock*. It does not claim that a laboratory
with the device for a week cannot later determine that a domain was destroyed
rather than never created. That is a much harder property, it would constrain
storage layout and wear levelling, and claiming it without doing that work would
be exactly the kind of marketing this project's documents refuse to produce.

**Anything about coercion during a lockout.** Credentials are not evaluated while
the device is locked, so a duress credential entered then does nothing. The
attacker gains nothing either, since the device will not open - but the user
cannot signal, and that is a real gap rather than a solved case.

**A silent alarm.** Deliberately absent. It cannot be made reliable on a device
the attacker controls, and shipping one would invite users to rely on it in
exactly the situation where it fails.

*(Auto-wipe after N failures was listed here as deliberately absent. That
position was wrong and has been reversed - see below.)*

## What is not decided yet

**The credential rule itself.** What makes something a member of the duress
family - the reference's 5-dictionary and 5-click are instantiations, not the
only ones - is a usability decision that needs work with real people, because a
rule nobody can apply under duress is a rule that does not exist. The authority
takes a classification precisely so this can be settled separately and changed
without touching any of the logic above.

**Wiring destruction to the Key Service.** The directive is emitted; M2.8's
`system.keys` acting on it, within the timing envelope, is the next step and is
where the envelope stops being an assertion and becomes a measurement.

**The lock screen itself.** Trusted presentation exists (M3.2) and consent
binding exists (M4.9); an unlock surface that an application cannot imitate is
the remaining UI work.

**Hardware-rooted resistance to rollback of the destruction.** Right now
destruction is as durable as the storage beneath it. `MonotonicSecurityState` is
still an interface boundary only, as M2 records, and until it is real an attacker
with an image of the device taken beforehand is outside what this defends.


## M4.10a - Erasure after repeated failure

This was originally recorded as a deliberate non-feature, on the grounds that it
is a denial of service anyone holding the phone for two minutes can mount. The
project owner overruled it, and the objection turns out to be answerable rather
than fatal.

**The denial of service is priced out by the backoff that already exists.**
Reaching a threshold of ten takes roughly a quarter of an hour of uninterrupted
wrong entries, because attempts five onward each cost an escalating lockout. Two
minutes with the phone is not enough. The reference platform reaches the same
arrangement from the same direction: escalating delays first, and erasure at ten
as something the owner may switch on.

**Overwriting cannot deliver what this feature promises.** The reference is
explicit, and it is the single most important technical fact here: securely
erasing keys "is especially challenging to do so on flash storage, where
wear-leveling might mean multiple copies of data need to be erased." A flash
translation layer keeps copies at addresses nothing above it can name. Any design
that answers "wipe the device" by writing over the data is making a promise the
storage stack will not keep.

So erasure is **destruction of the key**, and it emits the same directive the
duress path does. There is exactly one way to make data unrecoverable in this
system, reached by two different judgements - which is also why there is one code
path to review rather than two.

**Whether that defeats a laboratory is a property of the hardware, and is asked
rather than assumed.** The reference platform solves this with storage dedicated
to the purpose, addressing and erasing blocks at a very low level. ENML cannot
assume such hardware exists, so `PlatformErasure` records what the platform can
actually do and `forensic_erasure_available()` reports it. On hardware that
cannot truly efface, the setting is still worth having and the shell must not
describe it as putting data beyond recovery. This is M5.5's discipline again: a
device that cannot back a claim does not make it. Showing "device erased" over a
flash layer still holding readable copies would be lying to the owner at the
moment it mattered most.

**The threshold is the owner's, within bounds, and refused rather than clamped.**
Below five the feature stops distinguishing an attacker from an owner with cold
hands. Above twenty the backoff has already made the attempt cost hours. A
threshold outside those is refused, because silently enforcing a different number
from the one displayed means the setting shown is not the setting in force.

**And there is deliberately no default.** `erasure_choice_made()` starts false and
the shell must not complete setup while it is. Defaulting it off makes the device
quietly weaker than its owner believes; defaulting it on destroys somebody's
photographs the first time a child guesses at a lock screen. Both failure modes
are silent, and what they share is that nobody chose. A setting whose wrong value
cannot be undone is one the owner has to be asked about once, in words, while
nothing is on fire. Declining is recorded as a decision, so the device can tell
"the owner said no" from "nobody has been asked".

### Still not claimed

Erasure is as durable as the counter behind it. An attacker who can power-cycle
the device to reset `consecutive_invalid_attempts` defeats the threshold
entirely, and nothing in this tree yet makes that counter survive a reboot -
`MonotonicSecurityState` remains an interface boundary only, as M2 records. Until
it is real, the threshold protects against someone guessing at a lock screen and
not against someone willing to pull the battery between attempts.

## M4.10d - Non-interruptibility, and a source authorised outside the library

The project owner authorised consulting sources outside the supplied reference
library for this question. `docs/REFERENCE_ADDITIONS_2026_08_10.md` forbids that
by default and names the owner as the only party who can lift it, so the
authorisation is recorded here rather than left implicit. What follows is the one
requirement that came from outside the library; everything else in M4.10 is from
the supplied set.

The only mainstream phone OS shipping a duress password states its own bar
plainly: the wipe must guarantee data is unrecoverable **with no way to interrupt
it**, and rebooting into a recovery environment to wipe is explicitly not
acceptable. Entering it is indistinguishable from an ordinary unlock at the lock
screen.

Two of those ENML already had. The third is a requirement this design had not
written down, and it is the one that decides the mechanism:

**Erasure must complete without a reboot and must not be interruptible.** An
attacker who sees a device restart into anything unusual knows what happened and
pulls the battery. A wipe that walks a filesystem can be interrupted halfway, and
half a wipe is a device that still holds the half that mattered.

This is the strongest argument yet for cryptographic erasure over overwriting,
and it is a different argument from the flash-wear one: destroying a single
wrapping key is one small write that either happened or did not. There is no
state in which it is half done. Overwriting user data is inherently interruptible
because it is inherently long, and no amount of care makes a long operation
atomic.

So the directive M4.10a emits is not "begin wiping". It is "destroy this key",
and the key service must treat it as an operation that completes before anything
else observes the device - not a job queued for a reboot to finish.

### What this does not yet do

Nothing in the tree enforces the non-interruptibility. `system.keys` acting on
the directive within the uniform envelope is the wiring M4.10 already recorded as
outstanding, and this adds a requirement to it rather than satisfying one.

## M4.10e - Repeated failure is duress

Direction from the project owner: brute force should be considered duress, and
entering the wrong credential enough times should be treated as duress. That is a
correction to the architecture rather than a rename, and it improves it.

M4.10a and the duress path had become two routes to the same destruction. One
concept with two triggers should be one reaction, and a reaction that varies by
trigger is a way to tell the triggers apart - the screening problem the whole
design is built to avoid, reappearing in a new place. So reaching the threshold
now takes the duress *response* as well: the protected domain is destroyed and
the device presents an ordinary unlock onto whatever survives.

**The observable half is the point.** Refusing at the threshold tells an attacker
the data is still there and that guessing is not the way in. That is not a dead
end for him; it is a signpost, and what it points at is the owner. Coercing the
owner is precisely what the rest of this document defends against, so a lock
screen that ends a failed brute-force attempt by advertising "keep trying
something else" has made the situation worse. An unlock onto an empty device ends
the attempt instead: he believes he is through, and there is nothing behind it.

**What is granted is unprotected by construction.** The protected domain has no
key by the time access is released. Granting the remainder to someone who guessed
wrong ten times asserts only that unprotected data is unprotected.

**The owner who trips it opted in.** They get exactly what a duress unlock gets,
which is the cost of the setting and is why the setting has no default and must
be chosen in words during setup.

The device also stops refusing after the threshold, and continues to present the
granted response. A device that reverted to refusals would be announcing that the
threshold had been reached, which is the same disclosure by a slower route -
the same reasoning that refuses escalating an anonymous verifier to linked.

### Still not claimed

What "whatever survives" contains is not specified anywhere in the tree. A decoy
profile that is convincing, and that does not itself leak the existence of the
protected one, is unbuilt and is a harder problem than the policy above. Until it
exists, the granted response is honest about the mechanism and silent about the
experience.

## M4.10f - No trace, and the one this design was leaving

Direction from the project owner: when duress is triggered, user data is wiped
with no trace. Crypto-erasure already delivers the data half. The "no trace"
half was being violated by this design's own persistence, added two milestones
earlier for a good reason.

`PersistedUnlockState` stored `protected_domain_destroyed`, so that a restarted
device would not be asked to destroy the same domain twice. That boolean is
**evidence that destruction happened**, written to durable storage on purpose,
and it survives precisely because it was made durable. An examiner reading it
learns a duress event occurred, which in a coercion scenario is the fact that
gets somebody hurt. The feature intended to protect the owner was recording that
they had used it.

It is replaced by `DomainPresence`, which the key service supplies from what it
actually finds. A key that is not there is not there whether it was destroyed an
hour ago or never created, so deriving the state from ground truth carries the
same information with none of the confession.

The erasure settings were a second trace, and a subtler one. A device still
carrying "erase after five attempts" is a device announcing that its owner
anticipated coercion - which is a statement about the owner, not about the data,
and it outlives the data. They are now cleared when the domain is destroyed, so
the persisted record of a wiped device is byte-for-byte the record of a device
that was never configured. A test asserts exactly that equality, and asserts that
it is not comparing a wiped record against an identical unwiped one.

The owner who trips this loses the setting along with the data it protected, and
that is the correct pairing: a setting guarding nothing is not a protection, it
is a statement.

### What "no trace" still does not cover

The claim is now defensible at this layer and nowhere else. Trace-freedom is a
whole-system property and the rest of the system has not been audited for it:
what `system.keys` writes when it destroys, what the storage layer leaves in a
flash translation layer it does not control, what any log or crash record
retains, and whether a surviving profile is shaped like one that never had a
sibling. Any of those can reintroduce the confession this milestone removed.

M4.10 declined to claim forensic indistinguishability after the fact. That
declination stands. What changed is that this component no longer actively
undermines it.
