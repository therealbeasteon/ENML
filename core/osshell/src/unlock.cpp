#include <os/shell/unlock.hpp>

namespace os::shell {
namespace {

// The reference's requirement, made a build error rather than a comment.
//
// The lock has to outlast the window that defines a coercion episode. If it did
// not, the owner would come back after a lockout, enter the credential she
// normally uses, still be inside the window against the attacker's credential,
// and lock herself out again - permanently, through no fault of her own. That
// t2 > t1 is exactly what keeps a defence against coercion from becoming a
// denial of service against the person it protects.
static_assert(
    iteration_lock_nanoseconds > iteration_window_nanoseconds,
    "the iteration lock must outlast the iteration window, or a single coercion "
    "episode locks the owner out of the device for good");

// Ceiling on the counter rather than on the delay it produces. The delay is
// already capped; this only stops the counter itself from growing without bound
// on a device someone leaves guessing for a very long time.
inline constexpr std::uint32_t maximum_counted_attempts = 1024U;

[[nodiscard]] constexpr std::uint64_t later_of(std::uint64_t a, std::uint64_t b) noexcept {
    return (a > b) ? a : b;
}

} // namespace

UnlockAuthority::UnlockAuthority(PlatformErasure erasure) noexcept
    : erasure_(valid_platform_erasure(erasure) ? erasure : PlatformErasure::best_effort) {
    // An unrecognised value degrades to the weaker claim rather than the
    // stronger one. Every other default in this file fails closed; a platform
    // capability that failed *open* would have the device promising a laboratory
    // could not recover the data because an enum was out of range.
}

PersistedUnlockState UnlockAuthority::persisted_state() const noexcept {
    return PersistedUnlockState{
        invalid_attempts_, destroyed_, erasure_enabled_, erasure_chosen_, erasure_threshold_};
}

os::core::Result<void> UnlockAuthority::restore(const PersistedUnlockState& state) noexcept {
    if (state.erasure_enabled &&
        (state.erasure_threshold < minimum_erasure_threshold ||
         state.erasure_threshold > maximum_erasure_threshold)) {
        return shell_error(errors::invalid_erasure_threshold);
    }
    // Enabled without ever having been chosen is not a state this code can
    // produce, so encountering it means the record did not come from this code.
    if (state.erasure_enabled && !state.erasure_choice_made) {
        return shell_error(errors::invalid_erasure_threshold);
    }

    invalid_attempts_ = state.invalid_attempts;
    destroyed_ = state.protected_domain_destroyed;
    erasure_enabled_ = state.erasure_enabled;
    erasure_chosen_ = state.erasure_choice_made;
    erasure_threshold_ = state.erasure_threshold;
    // Time-derived state is deliberately not restored. See PersistedUnlockState.
    return {};
}

bool UnlockAuthority::protected_domain_destroyed() const noexcept {
    return destroyed_;
}

bool UnlockAuthority::erasure_enabled() const noexcept {
    return erasure_enabled_;
}

bool UnlockAuthority::erasure_choice_made() const noexcept {
    return erasure_chosen_;
}

std::uint32_t UnlockAuthority::erasure_threshold() const noexcept {
    return erasure_threshold_;
}

bool UnlockAuthority::forensic_erasure_available() const noexcept {
    return erasure_ == PlatformErasure::effaceable;
}

os::core::Result<void> UnlockAuthority::configure_erasure(
    bool enabled,
    std::uint32_t threshold) noexcept {
    if (enabled &&
        (threshold < minimum_erasure_threshold || threshold > maximum_erasure_threshold)) {
        return shell_error(errors::invalid_erasure_threshold);
    }
    erasure_enabled_ = enabled;
    // Recorded whichever way it went. Declining is a decision, and a device that
    // cannot tell "the owner said no" from "nobody has been asked" will ask
    // again forever or never ask at all.
    erasure_chosen_ = true;
    if (enabled) {
        erasure_threshold_ = threshold;
    }
    return {};
}

std::uint32_t UnlockAuthority::consecutive_invalid_attempts() const noexcept {
    return invalid_attempts_;
}

std::uint64_t UnlockAuthority::locked_until_nanoseconds() const noexcept {
    return locked_until_;
}

void UnlockAuthority::note_enrollment() noexcept {
    // Only the iteration history. Enrolling a credential is not a way to clear a
    // lockout, and it is emphatically not a way to bring back a destroyed
    // domain - if it were, "change your PIN" would be an instruction an attacker
    // could give.
    last_valid_tag_ = invalid_credential_tag_value;
    last_valid_at_ = 0U;
    have_valid_history_ = false;
}

std::uint64_t UnlockAuthority::backoff_for(std::uint32_t invalid_attempts) const noexcept {
    if (invalid_attempts < attempts_before_backoff) return 0U;

    const std::uint32_t steps = invalid_attempts - attempts_before_backoff;
    std::uint64_t backoff = initial_backoff_nanoseconds;
    for (std::uint32_t step = 0U; step < steps; ++step) {
        // Checked before doubling rather than after, so the value never leaves
        // the range it is allowed to be in. The ceiling also bounds this loop:
        // it cannot run more times than it takes to double from the initial
        // delay to the maximum.
        if (backoff >= maximum_backoff_nanoseconds / 2U) {
            return maximum_backoff_nanoseconds;
        }
        backoff *= 2U;
    }
    return (backoff > maximum_backoff_nanoseconds) ? maximum_backoff_nanoseconds : backoff;
}

os::core::Result<UnlockOutcome> UnlockAuthority::submit(
    std::uint64_t now_nanoseconds,
    CredentialClass classification,
    CredentialTag tag) noexcept {
    if (!valid_credential_class(classification)) {
        return os::core::Result<UnlockOutcome>{
            shell_error(errors::invalid_credential_class)};
    }

    // A valid credential with no tag would silently opt out of the iteration
    // rule, which is the one rule an attacker most wants disabled. An invalid
    // credential carrying a tag would enter the history and make an unrecognised
    // guess look like one of the user's own. Neither is interpreted.
    const bool is_valid_class = classification != CredentialClass::invalid;
    if (is_valid_class == (tag == invalid_credential_tag_value)) {
        return os::core::Result<UnlockOutcome>{shell_error(errors::invalid_credential_tag)};
    }

    // Monotonic time is a promise from below. If it is broken during an unlock,
    // every window and lockout here becomes meaningless, so this refuses rather
    // than computing on it. The refusal does not depend on the credential, so it
    // tells an observer nothing about which one was entered.
    if (now_nanoseconds < last_submit_) {
        return os::core::Result<UnlockOutcome>{shell_error(errors::unlock_time_reversed)};
    }
    last_submit_ = now_nanoseconds;

    // One envelope, computed before anything is decided and applied to every
    // path out of here. This is what makes destroying a key indistinguishable
    // from not destroying one: both answers arrive at the same moment.
    const std::uint64_t release_at = now_nanoseconds + uniform_unlock_nanoseconds;

    if (now_nanoseconds < locked_until_) {
        // The credential is not evaluated at all while locked - not classified
        // against, not counted, not entered into the history. A lockout an
        // attacker can grind against is not a lockout.
        return os::core::Result<UnlockOutcome>{UnlockOutcome{
            UnlockDisposition::locked,
            false,
            release_at,
            later_of(release_at, locked_until_)}};
    }

    if (classification == CredentialClass::invalid) {
        if (invalid_attempts_ < maximum_counted_attempts) ++invalid_attempts_;
        const std::uint64_t backoff = backoff_for(invalid_attempts_);
        if (backoff != 0U) {
            locked_until_ = now_nanoseconds + backoff;
        }

        // Erasure on repeated failure, if the owner asked for it.
        //
        // Destruction of the key, not overwriting of the data. The reference
        // platform is explicit that secure erasure is "especially challenging on
        // flash storage, where wear-leveling might mean multiple copies of data
        // need to be erased" - so overwriting is the one approach that cannot
        // deliver what this feature promises, and destroying the key that makes
        // the data readable is the one that can. It is the same directive the
        // duress path emits, because it is the same operation: exactly one way
        // to make data unrecoverable, reached by two different judgements.
        //
        // Whether it actually defeats a laboratory depends on the platform, and
        // `forensic_erasure_available()` is how a caller finds out rather than
        // assuming.
        const bool reached_threshold = erasure_enabled_ &&
            invalid_attempts_ >= erasure_threshold_;
        const bool erase = reached_threshold && !destroyed_;
        if (erase) destroyed_ = true;

        // Repeated failure *is* duress, so it takes the duress response too.
        //
        // The two were separate paths reaching the same destruction, which was
        // the wrong shape: one concept with two triggers is one reaction, and a
        // reaction that differs by trigger is a way to tell the triggers apart.
        //
        // The response that matters is the observable one. Refusing tells an
        // attacker the data is still there and that guessing is not the way in -
        // which points him at the owner, and the next thing he tries is the one
        // this whole document exists to defend against. Presenting an ordinary
        // unlock onto what survives ends the attack instead: he believes he is
        // through, and there is nothing behind it.
        //
        // What he is granted is by construction the domain that was not
        // protected, because the protected one no longer has a key. Granting it
        // to someone who guessed wrong ten times says only that unprotected data
        // is unprotected.
        //
        // The owner who reaches this by accident opted into it, and gets exactly
        // what a duress unlock gets. That is the cost of the setting and it is
        // stated where the setting is chosen.
        if (reached_threshold) {
            locked_until_ = 0U;
            invalid_attempts_ = 0U;
            return os::core::Result<UnlockOutcome>{UnlockOutcome{
                UnlockDisposition::granted, erase, release_at, release_at}};
        }

        // No history is written. An unrecognised guess is not one of the user's
        // credentials, so letting it set the iteration baseline would let an
        // attacker arm the lock by typing nonsense.
        return os::core::Result<UnlockOutcome>{UnlockOutcome{
            UnlockDisposition::refused,
            false,
            release_at,
            later_of(release_at, locked_until_)}};
    }

    // From here the credential is one of the user's own.

    // The panic reaction, and it happens first.
    //
    // Not conditional on the attempt being granted. Consider the attacker who
    // demands a second, different credential: if the duress one is the second,
    // the iteration rule below will refuse it - and if destruction waited on
    // being granted, the user would have spent her one signal on nothing. It is
    // also first because the attacker can cut power at any instant, and a
    // destruction that has not happened yet is a destruction that did not
    // happen.
    const bool destroy = (classification == CredentialClass::duress) && !destroyed_;
    if (destroy) destroyed_ = true;

    // The iteration rule. Note what it reads: a tag, a time, and nothing else.
    //
    // The class is deliberately not consulted and is deliberately not stored, so
    // the screening bug the reference warns about - locking on receipt of the
    // panic credential, which lets an attacker demand "one that does not lock"
    // and learn which is which - is not something this code could be edited into
    // doing by accident. Two different credentials of the user's own inside the
    // window lock the device, whichever came first.
    const bool within_window =
        have_valid_history_ &&
        (now_nanoseconds - last_valid_at_) <= iteration_window_nanoseconds;
    const bool iterated = within_window && tag != last_valid_tag_;

    last_valid_tag_ = tag;
    last_valid_at_ = now_nanoseconds;
    have_valid_history_ = true;
    invalid_attempts_ = 0U;

    if (iterated) {
        locked_until_ = now_nanoseconds + iteration_lock_nanoseconds;
        return os::core::Result<UnlockOutcome>{UnlockOutcome{
            UnlockDisposition::locked,
            destroy,
            release_at,
            later_of(release_at, locked_until_)}};
    }

    // Granted, and identical whether or not a domain was just destroyed. There
    // is no second success value for a duress unlock precisely so that no code
    // downstream of this point is able to tell.
    return os::core::Result<UnlockOutcome>{UnlockOutcome{
        UnlockDisposition::granted,
        destroy,
        release_at,
        release_at}};
}

} // namespace os::shell
