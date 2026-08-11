#include <cstdint>
#include <cstdio>

#include <os/core/error.hpp>
#include <os/shell/unlock.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "unlock: %s\n", what);
    }
    return condition;
}

template <typename T>
bool refused_with(const os::core::Result<T>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::shell &&
        result.error().code == code;
}

using os::shell::CredentialClass;
using os::shell::UnlockAuthority;
using os::shell::UnlockDisposition;
using os::shell::UnlockOutcome;

constexpr std::uint64_t second = 1'000'000'000U;
constexpr std::uint64_t window = os::shell::iteration_window_nanoseconds;
constexpr std::uint64_t lockout = os::shell::iteration_lock_nanoseconds;
constexpr std::uint64_t envelope = os::shell::uniform_unlock_nanoseconds;

// Opaque, and deliberately meaningless. The authority may only compare them.
constexpr os::shell::CredentialTag nominal_tag = 0xA1A1A1A1U;
constexpr os::shell::CredentialTag duress_tag = 0xB2B2B2B2U;
constexpr os::shell::CredentialTag other_duress_tag = 0xC3C3C3C3U;

UnlockOutcome must_submit(
    UnlockAuthority& authority,
    std::uint64_t now,
    CredentialClass klass,
    os::shell::CredentialTag tag) {
    auto result = authority.submit(now, klass, tag);
    if (!result) return UnlockOutcome{};
    return result.value();
}

} // namespace

int main() {
    // An ordinary unlock: granted, nothing destroyed, and released on the
    // uniform envelope rather than as fast as the comparison happened to run.
    {
        UnlockAuthority authority;
        const auto outcome = must_submit(authority, 0U, CredentialClass::nominal, nominal_tag);
        if (!check(outcome.disposition == UnlockDisposition::granted, "nominal was not granted")) {
            return 1;
        }
        if (!check(!outcome.destroy_protected_domain, "a nominal unlock destroyed something")) {
            return 1;
        }
        if (!check(outcome.release_at_nanoseconds == envelope, "wrong release time")) return 1;
        if (!check(!authority.protected_domain_destroyed(), "domain destroyed by a nominal unlock")) {
            return 1;
        }
    }

    // A duress unlock destroys the protected domain and then reports *exactly*
    // the same thing a nominal unlock reports. This is the property the whole
    // design turns on: past the destruction directive there is no value anywhere
    // that distinguishes the two, so no code downstream can be made to leak it.
    {
        UnlockAuthority nominal_authority;
        UnlockAuthority duress_authority;

        const auto normal =
            must_submit(nominal_authority, 0U, CredentialClass::nominal, nominal_tag);
        const auto panicked =
            must_submit(duress_authority, 0U, CredentialClass::duress, duress_tag);

        if (!check(panicked.disposition == UnlockDisposition::granted,
                   "duress did not present as an ordinary unlock")) return 1;
        if (!check(panicked.disposition == normal.disposition,
                   "duress and nominal report different dispositions")) return 1;
        if (!check(panicked.release_at_nanoseconds == normal.release_at_nanoseconds,
                   "duress and nominal are distinguishable by timing")) return 1;
        if (!check(panicked.accepting_again_at_nanoseconds ==
                       normal.accepting_again_at_nanoseconds,
                   "duress and nominal are distinguishable by when the device accepts again")) {
            return 1;
        }

        if (!check(panicked.destroy_protected_domain, "duress did not destroy the domain")) {
            return 1;
        }
        if (!check(duress_authority.protected_domain_destroyed(), "destruction was not recorded")) {
            return 1;
        }

        // One shot. The key service must not be told to destroy twice.
        const auto again =
            must_submit(duress_authority, second, CredentialClass::duress, duress_tag);
        if (!check(again.disposition == UnlockDisposition::granted,
                   "a second duress unlock was not granted")) return 1;
        if (!check(!again.destroy_protected_domain, "destruction was issued twice")) return 1;
    }

    // The iteration attack, in both orders. The attacker demands a second,
    // different credential; the device locks either way, so which one came first
    // is not something he can learn from the outcome. This is the reference's
    // 2P-lock requirement that the locking event be invariant to the type of
    // credential entered.
    {
        UnlockAuthority nominal_first;
        (void)must_submit(nominal_first, 0U, CredentialClass::nominal, nominal_tag);
        const auto a = must_submit(nominal_first, second, CredentialClass::duress, duress_tag);

        UnlockAuthority duress_first;
        (void)must_submit(duress_first, 0U, CredentialClass::duress, duress_tag);
        const auto b = must_submit(duress_first, second, CredentialClass::nominal, nominal_tag);

        if (!check(a.disposition == UnlockDisposition::locked,
                   "iterating from nominal to duress did not lock")) return 1;
        if (!check(b.disposition == UnlockDisposition::locked,
                   "iterating from duress to nominal did not lock")) return 1;
        if (!check(a.disposition == b.disposition,
                   "the order of the two credentials changed the disposition")) return 1;
        if (!check(a.accepting_again_at_nanoseconds == b.accepting_again_at_nanoseconds,
                   "the order of the two credentials changed the lockout")) return 1;
        if (!check(a.release_at_nanoseconds == b.release_at_nanoseconds,
                   "the order of the two credentials changed the timing")) return 1;

        // Both devices have destroyed, whichever order the attacker chose.
        if (!check(nominal_first.protected_domain_destroyed() &&
                       duress_first.protected_domain_destroyed(),
                   "an iteration order avoided destruction")) return 1;
    }

    // Duress destroys even when the attempt itself is refused by the iteration
    // rule. Otherwise the user's one signal would be spent on nothing, purely
    // because the attacker happened to demand the real credential first.
    {
        UnlockAuthority authority;
        (void)must_submit(authority, 0U, CredentialClass::nominal, nominal_tag);
        const auto outcome = must_submit(authority, second, CredentialClass::duress, duress_tag);
        if (!check(outcome.disposition == UnlockDisposition::locked, "the iteration did not lock")) {
            return 1;
        }
        if (!check(outcome.destroy_protected_domain,
                   "a refused duress attempt did not destroy")) return 1;
    }

    // Ordinary repeated use is not an iteration. The same credential, as often
    // as the owner likes, never locks anything.
    {
        UnlockAuthority authority;
        for (std::uint64_t i = 0U; i < 20U; ++i) {
            const auto outcome =
                must_submit(authority, i * second, CredentialClass::nominal, nominal_tag);
            if (!check(outcome.disposition == UnlockDisposition::granted,
                       "repeating one credential locked the device")) return 1;
        }
        if (!check(authority.locked_until_nanoseconds() == 0U, "the device locked itself")) {
            return 1;
        }
    }

    // Two different credentials far enough apart are not one coercion episode.
    {
        UnlockAuthority authority;
        (void)must_submit(authority, 0U, CredentialClass::nominal, nominal_tag);
        const auto outcome =
            must_submit(authority, window + second, CredentialClass::duress, duress_tag);
        if (!check(outcome.disposition == UnlockDisposition::granted,
                   "credentials outside the window were treated as an iteration")) return 1;
    }

    // The owner gets her device back. Because the lockout outlasts the window,
    // the credential she uses when it lifts is never still inside a coercion
    // episode - which is what stops a defence against coercion from becoming a
    // permanent lockout of the person it protects.
    {
        UnlockAuthority authority;
        (void)must_submit(authority, 0U, CredentialClass::duress, duress_tag);
        const auto locked = must_submit(authority, second, CredentialClass::nominal, nominal_tag);
        if (!check(locked.disposition == UnlockDisposition::locked, "the iteration did not lock")) {
            return 1;
        }

        const std::uint64_t after = second + lockout + second;
        const auto recovered = must_submit(authority, after, CredentialClass::nominal, nominal_tag);
        if (!check(recovered.disposition == UnlockDisposition::granted,
                   "the owner could not recover her device after the lockout")) return 1;
    }

    // While locked, nothing is evaluated - not counted, not entered into the
    // history, and not acted on. A lockout an attacker can grind against is not
    // a lockout, and that has to include the duress path.
    {
        UnlockAuthority authority;
        for (std::uint32_t i = 0U; i < os::shell::attempts_before_backoff; ++i) {
            (void)must_submit(authority, 0U, CredentialClass::invalid,
                              os::shell::invalid_credential_tag_value);
        }
        if (!check(authority.locked_until_nanoseconds() != 0U,
                   "guessing did not eventually lock the device")) return 1;

        const auto during = must_submit(authority, second, CredentialClass::duress, duress_tag);
        if (!check(during.disposition == UnlockDisposition::locked,
                   "a credential was evaluated during a lockout")) return 1;
        if (!check(!during.destroy_protected_domain, "a locked-out attempt destroyed the domain")) {
            return 1;
        }
        if (!check(!authority.protected_domain_destroyed(),
                   "destruction happened during a lockout")) return 1;
    }

    // Guessing is throttled, and escalates. The first few attempts cost nothing
    // so that a fumbled entry is not a punishment.
    {
        UnlockAuthority authority;
        std::uint64_t now = 0U;
        for (std::uint32_t i = 1U; i < os::shell::attempts_before_backoff; ++i) {
            const auto outcome = must_submit(authority, now, CredentialClass::invalid,
                                             os::shell::invalid_credential_tag_value);
            if (!check(outcome.disposition == UnlockDisposition::refused,
                       "an unrecognised credential was not refused")) return 1;
            if (!check(authority.locked_until_nanoseconds() == 0U,
                       "the device locked before its threshold")) return 1;
            now += second;
        }

        (void)must_submit(authority, now, CredentialClass::invalid,
                          os::shell::invalid_credential_tag_value);
        const std::uint64_t first_lock = authority.locked_until_nanoseconds();
        if (!check(first_lock == now + os::shell::initial_backoff_nanoseconds,
                   "the first backoff was not the initial one")) return 1;

        // The next failure after that lockout costs twice as much.
        now = first_lock + second;
        (void)must_submit(authority, now, CredentialClass::invalid,
                          os::shell::invalid_credential_tag_value);
        if (!check(authority.locked_until_nanoseconds() ==
                       now + (os::shell::initial_backoff_nanoseconds * 2U),
                   "the backoff did not escalate")) return 1;

        // And a real credential clears the count.
        now = authority.locked_until_nanoseconds() + second;
        (void)must_submit(authority, now, CredentialClass::nominal, nominal_tag);
        if (!check(authority.consecutive_invalid_attempts() == 0U,
                   "a successful unlock did not clear the guess count")) return 1;
    }

    // An unrecognised guess never becomes the iteration baseline. If it did, an
    // attacker could arm the lock by typing nonsense and then screen against it.
    {
        UnlockAuthority authority;
        (void)must_submit(authority, 0U, CredentialClass::invalid,
                          os::shell::invalid_credential_tag_value);
        const auto outcome = must_submit(authority, second, CredentialClass::nominal, nominal_tag);
        if (!check(outcome.disposition == UnlockDisposition::granted,
                   "a guess armed the iteration rule")) return 1;
    }

    // A third distinct credential is still an iteration - the rule is about
    // difference, not about there being exactly two.
    {
        UnlockAuthority authority;
        (void)must_submit(authority, 0U, CredentialClass::duress, duress_tag);
        const auto outcome =
            must_submit(authority, second, CredentialClass::duress, other_duress_tag);
        if (!check(outcome.disposition == UnlockDisposition::locked,
                   "two different duress credentials were not an iteration")) return 1;
    }

    // Changing your credentials is an ordinary thing to do and must not look
    // like coercion.
    {
        UnlockAuthority authority;
        (void)must_submit(authority, 0U, CredentialClass::nominal, nominal_tag);
        authority.note_enrollment();
        const auto outcome = must_submit(authority, second, CredentialClass::nominal, duress_tag);
        if (!check(outcome.disposition == UnlockDisposition::granted,
                   "enrolling a new credential locked the owner out")) return 1;
    }

    // Enrollment is not an escape hatch. It clears the history and nothing else.
    {
        UnlockAuthority authority;
        (void)must_submit(authority, 0U, CredentialClass::duress, duress_tag);
        (void)must_submit(authority, second, CredentialClass::nominal, nominal_tag);
        const std::uint64_t locked_until = authority.locked_until_nanoseconds();

        authority.note_enrollment();
        if (!check(authority.locked_until_nanoseconds() == locked_until,
                   "enrolling cleared a lockout")) return 1;
        if (!check(authority.protected_domain_destroyed(),
                   "enrolling undid the destruction")) return 1;
    }

    // Malformed submissions are refused rather than interpreted.
    {
        UnlockAuthority authority;
        if (!check(refused_with(authority.submit(0U, static_cast<CredentialClass>(0), 1U),
                                os::shell::errors::invalid_credential_class),
                   "an unknown credential class was accepted")) return 1;
        if (!check(refused_with(authority.submit(0U, CredentialClass::nominal,
                                                 os::shell::invalid_credential_tag_value),
                                os::shell::errors::invalid_credential_tag),
                   "a valid credential with no tag was accepted")) return 1;
        if (!check(refused_with(authority.submit(0U, CredentialClass::invalid, nominal_tag),
                                os::shell::errors::invalid_credential_tag),
                   "an invalid credential carrying a tag was accepted")) return 1;

        (void)must_submit(authority, 10U * second, CredentialClass::nominal, nominal_tag);
        if (!check(refused_with(authority.submit(second, CredentialClass::nominal, nominal_tag),
                                os::shell::errors::unlock_time_reversed),
                   "time going backwards was computed on")) return 1;
    }

    // Erasure on repeated failure is off unless the owner turns it on.
    // Irreversible destruction nobody asked for is a different product.
    {
        UnlockAuthority authority;
        if (!check(!authority.erasure_enabled(), "erasure was on by default")) return 1;
        // And the absence of a decision is distinguishable from a decision to
        // decline, so the shell knows it still has to ask.
        if (!check(!authority.erasure_choice_made(), "an unasked device claimed a decision")) {
            return 1;
        }

        std::uint64_t now = 0U;
        for (std::uint32_t i = 0U; i < 40U; ++i) {
            (void)must_submit(authority, now, CredentialClass::invalid,
                              os::shell::invalid_credential_tag_value);
            now = authority.locked_until_nanoseconds() + second;
        }
        if (!check(!authority.protected_domain_destroyed(),
                   "guessing destroyed the domain without the owner asking")) return 1;
    }

    // Turned on, it destroys the key at the chosen count - and it is the key,
    // not the data. Overwriting cannot survive a flash translation layer; a
    // destroyed key can.
    {
        UnlockAuthority authority;
        if (!check(static_cast<bool>(authority.configure_erasure(true, 5U)),
                   "configuring erasure was refused")) return 1;
        if (!check(authority.erasure_enabled() && authority.erasure_threshold() == 5U,
                   "the setting was not recorded")) return 1;

        std::uint64_t now = 0U;
        bool destroyed_on = false;
        for (std::uint32_t i = 1U; i <= 5U; ++i) {
            const auto outcome = must_submit(authority, now, CredentialClass::invalid,
                                             os::shell::invalid_credential_tag_value);
            if (outcome.destroy_protected_domain) {
                destroyed_on = true;
                if (!check(i == 5U, "erasure fired before the chosen count")) return 1;
            }
            now = authority.locked_until_nanoseconds() + second;
        }
        if (!check(destroyed_on, "the chosen count did not erase")) return 1;
        if (!check(authority.protected_domain_destroyed(), "destruction was not recorded")) {
            return 1;
        }

        // One shot. The key service must not be told to destroy twice.
        const auto after = must_submit(authority, now, CredentialClass::invalid,
                                       os::shell::invalid_credential_tag_value);
        if (!check(!after.destroy_protected_domain, "erasure was issued twice")) return 1;
    }

    // A correct credential before the threshold clears the count, so the owner
    // who fumbles and then succeeds keeps their data.
    {
        UnlockAuthority authority;
        (void)authority.configure_erasure(true, 5U);

        std::uint64_t now = 0U;
        for (std::uint32_t i = 0U; i < 4U; ++i) {
            (void)must_submit(authority, now, CredentialClass::invalid,
                              os::shell::invalid_credential_tag_value);
            now += second;
        }
        (void)must_submit(authority, now, CredentialClass::nominal, nominal_tag);
        if (!check(authority.consecutive_invalid_attempts() == 0U, "the count did not clear")) {
            return 1;
        }

        now += second;
        for (std::uint32_t i = 0U; i < 4U; ++i) {
            (void)must_submit(authority, now, CredentialClass::invalid,
                              os::shell::invalid_credential_tag_value);
            now += second;
        }
        if (!check(!authority.protected_domain_destroyed(),
                   "a successful unlock did not save the owner's data")) return 1;
    }

    // The threshold is bounded at both ends, and refused rather than clamped -
    // a setting shown to the user that is not the setting in force is worse than
    // no setting.
    {
        UnlockAuthority authority;
        if (!check(refused_with(
                       authority.configure_erasure(true, os::shell::minimum_erasure_threshold - 1U),
                       os::shell::errors::invalid_erasure_threshold),
                   "a threshold below the floor was accepted")) return 1;
        if (!check(refused_with(
                       authority.configure_erasure(true, os::shell::maximum_erasure_threshold + 1U),
                       os::shell::errors::invalid_erasure_threshold),
                   "a threshold above the ceiling was accepted")) return 1;
        if (!check(!authority.erasure_enabled(), "a refused configuration took effect")) return 1;

        // Turning it off needs no threshold at all.
        if (!check(static_cast<bool>(authority.configure_erasure(false, 0U)),
                   "disabling erasure was refused")) return 1;
        if (!check(authority.erasure_choice_made(),
                   "declining was not recorded as a decision")) return 1;
    }

    // What the platform can actually promise is asked, not assumed. On hardware
    // that cannot truly efface, the shell must not describe this as putting the
    // data beyond a laboratory.
    {
        UnlockAuthority best_effort{os::shell::PlatformErasure::best_effort};
        if (!check(!best_effort.forensic_erasure_available(),
                   "a best-effort platform claimed forensic erasure")) return 1;

        UnlockAuthority effaceable{os::shell::PlatformErasure::effaceable};
        if (!check(effaceable.forensic_erasure_available(),
                   "an effaceable platform did not report it")) return 1;

        // An unrecognised capability degrades to the weaker claim. Every other
        // default here fails closed and so does this one.
        UnlockAuthority garbled{static_cast<os::shell::PlatformErasure>(0)};
        if (!check(!garbled.forensic_erasure_available(),
                   "an invalid platform capability claimed forensic erasure")) return 1;
    }

    return 0;
}
