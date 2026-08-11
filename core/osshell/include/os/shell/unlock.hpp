#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/shell/error.hpp>

// Coercion-resistant unlock authority.
//
// `docs/ACHIEVEMENTS.md` has carried this as an unclaimed gap with a warning
// attached: a lock screen needs "a coercion-resistance design that is not a
// naive second panic PIN - the supplied duress reference shows why simple
// two-password schemes fail under repeated coercion."
// `docs/M4_10_COERCION_RESISTANT_UNLOCK.md` is the design; this is the part that
// has to be true in code.
//
// The reference on panic passwords is unusually direct about what fails. Giving
// the user a regular password and one panic password is defeated by an
// **iteration attack**: the attacker simply demands a second authentication with
// a different password, and the user, having only two, must eventually give up
// the real one. It is further weakened by a **forced-randomization attack**,
// where the attacker makes the user disclose every password she knows and then
// picks which to use himself. And it warns about the repair that looks obvious:
// locking on receipt of the panic password lets the attacker *screen* - he
// demands a password that does not lock the device, and learns which one it was.
// The event that locks must be invariant to which credential was entered.
//
// Where ENML has to depart from that reference is more important than where it
// follows, and it comes down to one line in its threat model: Bob - the verifier
// - is a trusted party, elsewhere, not in collusion with the attacker. Every
// scheme there can therefore take an *unobserved reaction*: a silent alarm, a
// spoiled ballot, a note in a database the attacker cannot see.
//
// A phone has no such party. The verifier is the device, and the device is in
// the attacker's hand. Three consequences shape everything below.
//
//   - **There is no unobserved channel, so the reaction must be local and
//     irreversible.** A silent alarm needs a network the attacker removes by
//     holding down the power button, pulling the SIM, or standing in a basement.
//     A design that depends on a message reaching anyone fails exactly when it
//     is needed. What a phone *can* do unobserved is destroy a key, and after
//     that no amount of later access recovers what it protected. ENML's panic
//     reaction is destruction of the protected key domain, performed before any
//     access is granted, because the attacker may cut power at any moment.
//
//   - **The observable response must be a normal unlock.** Not a refusal, not a
//     wipe animation, not a slower screen. The duress credential opens the
//     device onto what survives, which is what the attacker asked for and cannot
//     distinguish from what he wanted.
//
//   - **Duration is a channel, because the attacker is holding the stopwatch.**
//     Destroying a key takes longer than not destroying one. So the authority
//     does not merely try to balance its branches, it states a single deadline
//     before which no result may be shown, identical for every disposition. A
//     uniform envelope is one rule to verify; per-path balancing is how a timing
//     channel comes back the first time somebody adds a case.
//
// Two further properties are required of the classifier that feeds this, and are
// stated here because the authority cannot enforce them and must not pretend to:
//
//   - The duress family must be **large**, so that a persistent attacker cannot
//     exhaust it by iteration - the failure that makes a single panic password
//     useless against anyone willing to ask twice.
//
//   - A mistyped nominal credential must be far more likely to land in
//     *invalid* than in *duress*. The reference makes this an explicit design
//     property: panic and invalid spaces must be well mixed, so that a slip of
//     the finger is an error message rather than the irreversible destruction of
//     everything the user owns.
//
// The authority never sees a credential. It is given a classification and an
// opaque tag, which is what lets it be tested and fuzzed with no secret material
// anywhere near it.
namespace os::shell {

// The uniform unlock envelope. No result is released before this long after the
// attempt, whatever happened.
//
// Chosen from both ends. Below it, a credential verifier is not doing enough
// work to be worth attacking through - key stretching is the actual defence
// against an attacker who images the device and grinds offline. Above it, a lock
// screen feels broken, and a user who thinks the phone did not register the
// press enters it again. 750 ms sits where those meet.
inline constexpr std::uint64_t uniform_unlock_nanoseconds = 750'000'000U;

// How long two *different* credentials count as one coercion episode.
//
// This is the reference's t1. It wants to span an attacker saying "now do it
// again with a different one", and nothing longer.
inline constexpr std::uint64_t iteration_window_nanoseconds = 300'000'000'000U;

// How long the device refuses after an iteration is detected. The reference
// requires t2 > t1, so that a non-persistent attacker - one who has to leave -
// gains nothing by asking twice.
inline constexpr std::uint64_t iteration_lock_nanoseconds = 1'800'000'000'000U;

// Unrecognised credentials tolerated before the device starts refusing.
inline constexpr std::uint32_t attempts_before_backoff = 5U;
inline constexpr std::uint64_t initial_backoff_nanoseconds = 30'000'000'000U;
inline constexpr std::uint64_t maximum_backoff_nanoseconds = 3'600'000'000'000U;

// Erasure after repeated wrong credentials is the owner's choice, and so is the
// count. The reference platform offers exactly this - users "can choose to have
// the device automatically wiped if the passcode is entered incorrectly after 10
// consecutive attempts", and the threshold can be set lower.
//
// Off unless chosen. Not because it is a weak feature - it is the strongest one
// here - but because irreversible destruction that a user did not ask for is a
// different product, and a setting nobody opted into is one they cannot be said
// to have understood.
inline constexpr std::uint32_t default_erasure_threshold = 10U;

// The threshold is bounded at both ends, and both bounds do work.
//
// Below the floor the feature stops distinguishing an attacker from an owner
// with cold hands: a threshold of two is a device that erases itself the second
// time somebody fumbles in the dark. Above the ceiling the escalating backoff has
// already made the attempt cost hours, so a larger number buys nothing an
// attacker notices and only delays the owner's protection.
inline constexpr std::uint32_t minimum_erasure_threshold = 5U;
inline constexpr std::uint32_t maximum_erasure_threshold = 20U;

// What the platform can actually promise about destroying a key.
//
// The reference platform is explicit that this is the hard part: securely
// erasing keys "is especially challenging to do so on flash storage, where
// wear-leveling might mean multiple copies of data need to be erased", and it
// solves it with storage dedicated to the purpose that addresses and erases
// blocks at a very low level.
//
// ENML cannot assume that hardware exists. It can refuse to claim what it does
// not have - the same discipline M5.5 applies to boot state, where a device
// without an immutable first stage is not permitted to describe itself as
// closed and verified. A phone that shows "device erased" while the flash
// translation layer still holds recoverable copies has told its owner something
// false at the moment it mattered most.
enum class PlatformErasure : std::uint8_t {
    // Key material lives where it can be destroyed for real. Destroying it makes
    // the data cryptographically unrecoverable, which is the only erasure that
    // survives a laboratory.
    effaceable = 1U,
    // Keys are destroyed as well as the storage stack allows, and no
    // forensic-grade claim is available. Honest, and much weaker.
    best_effort = 2U,
};

[[nodiscard]] constexpr bool valid_platform_erasure(PlatformErasure value) noexcept {
    switch (value) {
    case PlatformErasure::effaceable:
    case PlatformErasure::best_effort:
        return true;
    }
    return false;
}

// What the verifier concluded about a submitted credential.
//
// Producing this is a constant-time comparison against stored verifiers, and it
// happens outside this file. `os::core::constant_time_equal` exists for it; a
// classifier that returns faster for one class than another reintroduces exactly
// the channel the uniform envelope closes.
enum class CredentialClass : std::uint8_t {
    nominal = 1U,
    duress = 2U,
    invalid = 3U,
};

[[nodiscard]] constexpr bool valid_credential_class(CredentialClass value) noexcept {
    switch (value) {
    case CredentialClass::nominal:
    case CredentialClass::duress:
    case CredentialClass::invalid:
        return true;
    }
    return false;
}

// Distinguishes one valid credential from another without revealing either.
//
// The iteration rule needs to know whether this is the *same* credential as last
// time, and nothing else about it. An opaque tag answers exactly that question
// and no other, which is why the rule below can be written without ever
// consulting the class - and therefore cannot become a screen by accident.
using CredentialTag = std::uint64_t;
inline constexpr CredentialTag invalid_credential_tag_value = 0U;

// What the shell should do. Deliberately coarse.
//
// There is no `granted_under_duress`. A duress unlock and a nominal unlock
// return the *same* disposition, so that once destruction has happened no code
// path anywhere downstream can branch on which one occurred. The one-shot
// destruction directive is the only difference, and it is consumed before access
// is released rather than recorded as state anyone can query.
enum class UnlockDisposition : std::uint8_t {
    granted = 1U,
    refused = 2U,
    // Refused without the credential being evaluated at all.
    locked = 3U,
};

struct UnlockOutcome final {
    UnlockDisposition disposition {UnlockDisposition::refused};
    // Destroy the protected key domain, irreversibly, *before* releasing
    // anything. One-shot: it is true on the attempt that earns it and never
    // again.
    bool destroy_protected_domain {false};
    // The earliest time this result may be shown to anyone. Identical for every
    // disposition, so that how long an unlock took says nothing about what kind
    // it was.
    std::uint64_t release_at_nanoseconds {0U};
    // When the device will accept another attempt. Equal to the release time
    // when nothing is locked.
    std::uint64_t accepting_again_at_nanoseconds {0U};

    [[nodiscard]] friend constexpr bool
    operator==(const UnlockOutcome&, const UnlockOutcome&) = default;
};

class UnlockAuthority final {
public:
    UnlockAuthority() noexcept = default;
    explicit UnlockAuthority(PlatformErasure erasure) noexcept;

    // Turns erasure-on-repeated-failure on or off, and sets the count.
    //
    // The owner's decision, made once and applied to every attempt after it.
    // Refused outside the bounds above rather than clamped: silently accepting a
    // threshold of two and enforcing five would mean the setting shown to the
    // user is not the setting in force, and the user is the only party who can
    // weigh this trade.
    [[nodiscard]] os::core::Result<void> configure_erasure(
        bool enabled,
        std::uint32_t threshold = default_erasure_threshold) noexcept;

    [[nodiscard]] bool erasure_enabled() const noexcept;
    [[nodiscard]] std::uint32_t erasure_threshold() const noexcept;

    // Whether the owner has decided about erasure at all.
    //
    // False until `configure_erasure` is called either way, and the shell must
    // not complete setup while it is false. This is deliberately not a default.
    //
    // Defaulting it off makes the device quietly weaker than its owner believes;
    // defaulting it on destroys somebody's photographs the first time a child
    // guesses at a lock screen. Both failure modes are silent, and the thing
    // they have in common is that nobody chose. A setting whose wrong value
    // cannot be undone is one the owner has to be asked about once, in words,
    // while nothing is on fire.
    [[nodiscard]] bool erasure_choice_made() const noexcept;

    // Whether destroying a key on this platform actually defeats a laboratory.
    //
    // A static fact about the hardware, not a per-attempt one, so it is asked
    // rather than reported on an outcome. The shell needs it to describe the
    // setting honestly: on a platform that cannot truly efface, "erase all data"
    // is a weaker promise and saying so is the difference between a security
    // feature and a reassuring noise.
    [[nodiscard]] bool forensic_erasure_available() const noexcept;

    // Evaluates one attempt.
    //
    // `now_nanoseconds` is monotonic. A classification of `invalid` must carry
    // `invalid_credential_tag_value`, and any valid class must carry a non-zero
    // tag; mixing those is refused rather than interpreted, because a valid
    // credential with no tag would silently opt out of the iteration rule.
    os::core::Result<UnlockOutcome> submit(
        std::uint64_t now_nanoseconds,
        CredentialClass classification,
        CredentialTag tag) noexcept;

    // Records that the user deliberately changed their credentials.
    //
    // This clears the iteration history. Without it, enrolling a new nominal
    // credential and then using it would look exactly like an attacker asking
    // for a second, different password, and the device would lock its owner out
    // for half an hour for doing something entirely ordinary.
    void note_enrollment() noexcept;

    // Whether the protected domain has already been destroyed.
    //
    // For the key service, which must not be asked to destroy it twice. This is
    // not a secret being leaked: by the time anyone can call this, the data is
    // already gone and its absence is discoverable by other means. What matters
    // is that nothing reveals it *during* a coerced unlock, and the outcome of
    // `submit` does not.
    [[nodiscard]] bool protected_domain_destroyed() const noexcept;

    [[nodiscard]] std::uint32_t consecutive_invalid_attempts() const noexcept;
    // Zero when not locked.
    [[nodiscard]] std::uint64_t locked_until_nanoseconds() const noexcept;

private:
    [[nodiscard]] std::uint64_t backoff_for(std::uint32_t invalid_attempts) const noexcept;

    // Iteration history. Deliberately a tag and a time, with no room to record a
    // class - the rule cannot consult what is not stored.
    CredentialTag last_valid_tag_ {invalid_credential_tag_value};
    std::uint64_t last_valid_at_ {0U};
    bool have_valid_history_ {false};

    std::uint32_t invalid_attempts_ {0U};
    std::uint64_t locked_until_ {0U};
    std::uint64_t last_submit_ {0U};
    bool destroyed_ {false};

    PlatformErasure erasure_ {PlatformErasure::best_effort};
    bool erasure_enabled_ {false};
    bool erasure_chosen_ {false};
    std::uint32_t erasure_threshold_ {default_erasure_threshold};
};

} // namespace os::shell
