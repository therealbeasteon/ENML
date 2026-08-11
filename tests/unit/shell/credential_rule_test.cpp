#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <os/core/error.hpp>
#include <os/shell/credential_rule.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "credential rule: %s\n", what);
    }
    return condition;
}

std::size_t distance_of(const char* a, const char* b) {
    auto result = os::shell::damerau_levenshtein(
        reinterpret_cast<const std::uint8_t*>(a), std::strlen(a),
        reinterpret_cast<const std::uint8_t*>(b), std::strlen(b));
    return result ? result.value() : 9999U;
}

bool enrollment_ok(const char* nominal, const char* duress) {
    return static_cast<bool>(os::shell::validate_duress_enrollment(
        reinterpret_cast<const std::uint8_t*>(nominal), std::strlen(nominal),
        reinterpret_cast<const std::uint8_t*>(duress), std::strlen(duress)));
}

bool enrollment_refused(const char* nominal, const char* duress, std::uint32_t code) {
    auto r = os::shell::validate_duress_enrollment(
        reinterpret_cast<const std::uint8_t*>(nominal), std::strlen(nominal),
        reinterpret_cast<const std::uint8_t*>(duress), std::strlen(duress));
    return !r && r.error().domain == os::core::ErrorDomain::shell && r.error().code == code;
}

} // namespace

int main() {
    // The metric behaves as the reference describes it.
    if (!check(distance_of("1234", "1234") == 0U, "identical strings are not distance zero")) {
        return 1;
    }
    if (!check(distance_of("1234", "1235") == 1U, "one substitution is not one edit")) return 1;
    if (!check(distance_of("1234", "123") == 1U, "one deletion is not one edit")) return 1;
    if (!check(distance_of("1234", "12345") == 1U, "one insertion is not one edit")) return 1;

    // A transposition is ONE edit, not two. Swapping two digits is among the
    // commonest ways to fumble a PIN, and a metric that scored it as two would
    // under-count exactly the risk this check exists to catch.
    if (!check(distance_of("1234", "1243") == 1U, "a transposition was not one edit")) return 1;

    // Unrestricted Damerau, not the restricted variant. This is the classic case
    // where the two disagree: restricted alignment says three, the true metric
    // says two. Reporting three here would wave through a pair that is closer
    // than the floor, which is the error in the unsafe direction.
    if (!check(distance_of("CA", "ABC") == 2U,
               "the restricted variant was computed instead of the true metric")) return 1;

    // Enrollment: a duress credential a typo away from the real one is refused.
    if (!check(enrollment_refused("482913", "482913",
                                  os::shell::errors::duress_credential_too_similar),
               "the same credential was accepted for both roles")) return 1;
    if (!check(enrollment_refused("482913", "482914",
                                  os::shell::errors::duress_credential_too_similar),
               "one substitution away was accepted")) return 1;
    if (!check(enrollment_refused("482913", "482931",
                                  os::shell::errors::duress_credential_too_similar),
               "one transposition away was accepted")) return 1;
    if (!check(enrollment_refused("482913", "48291",
                                  os::shell::errors::duress_credential_too_similar),
               "one deletion away was accepted")) return 1;
    if (!check(enrollment_refused("482913", "482813",
                                  os::shell::errors::duress_credential_too_similar),
               "two edits away was accepted")) return 1;

    // Far enough apart is fine.
    if (!check(enrollment_ok("482913", "735602"), "a well-separated pair was refused")) return 1;

    // Lengths are bounded at both ends rather than truncated.
    if (!check(enrollment_refused("123", "987654", os::shell::errors::invalid_credential_length),
               "a too-short credential was accepted")) return 1;

    char oversized[os::shell::max_credential_bytes + 8U];
    for (std::size_t i = 0U; i < sizeof(oversized) - 1U; ++i) oversized[i] = 'a';
    oversized[sizeof(oversized) - 1U] = '\0';
    if (!check(enrollment_refused("482913", oversized,
                                  os::shell::errors::invalid_credential_length),
               "an over-length credential was accepted")) return 1;

    return 0;
}
