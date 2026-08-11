#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/shell/error.hpp>

// Enrollment separation between a nominal credential and a duress credential.
//
// `docs/M4_10_COERCION_RESISTANT_UNLOCK.md` states an obligation the unlock
// authority cannot enforce and calls it the most dangerous requirement in the
// design: a mistyped nominal credential must be far more likely to land in
// *invalid* than in *duress*. Get it wrong and a slip of the finger irreversibly
// destroys everything the owner has.
//
// Most of that obligation belongs to whatever rule generates the duress family,
// and is still open. But one part of it is checkable right now, at the one
// moment both credentials are legitimately in the same place: enrollment. If the
// owner picks a duress credential one keystroke away from their real one, a
// single typo is a catastrophe, and the device is the only party positioned to
// notice before it happens.
//
// The metric is the reference's own. It names Damerau-Levenshtein distance -
// insertions, deletions, substitutions and transpositions of adjacent
// characters - as "a good metric for passwords", chosen because it was designed
// for spell-checking and therefore models the mistakes people actually make. A
// transposition is one edit here precisely because swapping two digits is one of
// the commonest ways to fumble a PIN, and a metric that scored it as two would
// under-count the risk this check exists to catch.
//
// The reference is also honest that the property is "necessary but not
// sufficient", and that confirming it would need empirical typo data. So this is
// a floor, not a proof: it rules out the pairs that are obviously unsafe. It
// does not establish that the survivors are safe.
namespace os::shell {

// Credentials longer than this are refused rather than truncated. The distance
// computation is quadratic, and an unbounded one at enrollment is a caller-
// controlled cost in a component that runs on a phone.
inline constexpr std::size_t max_credential_bytes = 64U;

// Shorter than this and the separation check is meaningless, because there is
// not enough credential for three edits to be a meaningful distance.
inline constexpr std::size_t minimum_credential_bytes = 4U;

// How far apart a duress credential must be from the nominal one.
//
// Three edits. Ordinary slips - one wrong key, one missed key, one doubled key,
// two keys swapped - are one edit, and two independent slips in one entry are
// already unusual. Requiring three means no single fumble and no plausible
// double fumble can turn an attempt to unlock into an irreversible erasure.
//
// This is a floor on the *owner's own choice of pair*, which is the only part of
// the typo problem that can be checked without knowing the credential rule.
inline constexpr std::size_t minimum_duress_separation = 3U;

// Damerau-Levenshtein distance, bounded.
//
// Not constant-time, and deliberately so: this runs once at enrollment on two
// credentials the owner has just chosen and is about to be told about, not on an
// attacker's guess at a lock screen. Making it constant-time would suggest it
// belonged on the authentication path, which is exactly where it must not be -
// the authority never sees a credential at all.
[[nodiscard]] os::core::Result<std::size_t> damerau_levenshtein(
    const std::uint8_t* first,
    std::size_t first_length,
    const std::uint8_t* second,
    std::size_t second_length) noexcept;

// Refuses an enrollment whose duress credential is close enough to the nominal
// one that a typo could reach it.
//
// Neither credential is retained. This is the one moment both are legitimately
// in the same place, and the check exists so that the owner finds out now rather
// than the first time they are cold, hurried or frightened.
[[nodiscard]] os::core::Result<void> validate_duress_enrollment(
    const std::uint8_t* nominal,
    std::size_t nominal_length,
    const std::uint8_t* duress,
    std::size_t duress_length) noexcept;

} // namespace os::shell
