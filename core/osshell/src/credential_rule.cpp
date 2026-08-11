#include <os/shell/credential_rule.hpp>

#include <array>

namespace os::shell {
namespace {

// Unrestricted Damerau-Levenshtein, not the restricted "optimal string
// alignment" variant, and the difference matters in the unsafe direction.
//
// OSA is simpler and is what most implementations labelled Damerau actually
// compute, but it never reports a distance *below* the true one - it can report
// above. This check accepts a pair when the distance is at least three, so an
// over-estimate is exactly the error that would wave through a pair the true
// metric says is closer than three. Getting that wrong here means accepting an
// enrollment where a typo destroys the owner's data, so the algorithm with the
// alphabet table is the one worth writing.
[[nodiscard]] std::uint16_t smallest_of(
    std::uint16_t a,
    std::uint16_t b,
    std::uint16_t c,
    std::uint16_t d) noexcept {
    std::uint16_t best = a;
    if (b < best) best = b;
    if (c < best) best = c;
    if (d < best) best = d;
    return best;
}

} // namespace

os::core::Result<std::size_t> damerau_levenshtein(
    const std::uint8_t* first,
    std::size_t first_length,
    const std::uint8_t* second,
    std::size_t second_length) noexcept {
    if (first == nullptr || second == nullptr) {
        return os::core::Result<std::size_t>{shell_error(errors::invalid_credential_length)};
    }
    if (first_length > max_credential_bytes || second_length > max_credential_bytes) {
        return os::core::Result<std::size_t>{shell_error(errors::invalid_credential_length)};
    }

    const std::size_t n = first_length;
    const std::size_t m = second_length;

    // Fixed, so there is no allocator behind a check that runs during setup on a
    // phone. Bounded by max_credential_bytes, which is why over-length inputs
    // are refused above rather than clamped.
    std::array<std::array<std::uint16_t, max_credential_bytes + 2U>, max_credential_bytes + 2U>
        distance{};
    std::array<std::size_t, 256U> last_row_with{};

    const auto infinity = static_cast<std::uint16_t>(n + m);
    distance[0][0] = infinity;
    for (std::size_t i = 0U; i <= n; ++i) {
        distance[i + 1U][0] = infinity;
        distance[i + 1U][1] = static_cast<std::uint16_t>(i);
    }
    for (std::size_t j = 0U; j <= m; ++j) {
        distance[0][j + 1U] = infinity;
        distance[1][j + 1U] = static_cast<std::uint16_t>(j);
    }

    for (std::size_t i = 1U; i <= n; ++i) {
        std::size_t last_match_column = 0U;
        for (std::size_t j = 1U; j <= m; ++j) {
            const std::size_t matched_row = last_row_with[second[j - 1U]];
            const std::size_t matched_column = last_match_column;

            std::uint16_t cost = 1U;
            if (first[i - 1U] == second[j - 1U]) {
                cost = 0U;
                last_match_column = j;
            }

            const auto substitution = static_cast<std::uint16_t>(distance[i][j] + cost);
            const auto insertion = static_cast<std::uint16_t>(distance[i + 1U][j] + 1U);
            const auto deletion = static_cast<std::uint16_t>(distance[i][j + 1U] + 1U);
            // The transposition arm, and the reason for the alphabet table: it
            // costs one edit to swap two characters however far apart the
            // algorithm had to look to pair them up.
            const auto transposition = static_cast<std::uint16_t>(
                distance[matched_row][matched_column] +
                (i - matched_row - 1U) + 1U + (j - matched_column - 1U));

            distance[i + 1U][j + 1U] =
                smallest_of(substitution, insertion, deletion, transposition);
        }
        last_row_with[first[i - 1U]] = i;
    }

    return os::core::Result<std::size_t>{static_cast<std::size_t>(distance[n + 1U][m + 1U])};
}

os::core::Result<void> validate_duress_enrollment(
    const std::uint8_t* nominal,
    std::size_t nominal_length,
    const std::uint8_t* duress,
    std::size_t duress_length) noexcept {
    if (nominal == nullptr || duress == nullptr) {
        return shell_error(errors::invalid_credential_length);
    }
    if (nominal_length < minimum_credential_bytes || duress_length < minimum_credential_bytes ||
        nominal_length > max_credential_bytes || duress_length > max_credential_bytes) {
        return shell_error(errors::invalid_credential_length);
    }

    auto separation = damerau_levenshtein(nominal, nominal_length, duress, duress_length);
    if (!separation) {
        return separation.error();
    }
    // Zero distance is the same credential, which would make the duress path
    // unreachable - the owner would believe they had a signal they do not have.
    // It falls out of the separation check, and is worth naming because it is
    // the failure that looks most like success.
    if (separation.value() < minimum_duress_separation) {
        return shell_error(errors::duress_credential_too_similar);
    }
    return {};
}

} // namespace os::shell
