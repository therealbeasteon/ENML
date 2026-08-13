#pragma once

#include <cstdint>

#include <os/core/error.hpp>

namespace os::shell {

namespace errors {
inline constexpr std::uint32_t invalid_task = 1U;
inline constexpr std::uint32_t task_capacity = 2U;
inline constexpr std::uint32_t task_conflict = 3U;
inline constexpr std::uint32_t unknown_task = 4U;
inline constexpr std::uint32_t revision_exhausted = 5U;
inline constexpr std::uint32_t activation_serial_exhausted = 6U;
inline constexpr std::uint32_t invalid_lifecycle_snapshot = 7U;
inline constexpr std::uint32_t stale_lifecycle_snapshot = 8U;
inline constexpr std::uint32_t invalid_scene_snapshot = 9U;
inline constexpr std::uint32_t stale_scene_snapshot = 10U;
inline constexpr std::uint32_t invalid_activation_intent = 11U;
inline constexpr std::uint32_t stale_activation_intent = 12U;
inline constexpr std::uint32_t preview_capture_denied = 13U;
inline constexpr std::uint32_t stale_preview_grant = 14U;
inline constexpr std::uint32_t invalid_consent_request = 15U;
inline constexpr std::uint32_t untrusted_consent_presentation = 16U;
inline constexpr std::uint32_t stale_consent_answer = 17U;
inline constexpr std::uint32_t consent_already_answered = 18U;
inline constexpr std::uint32_t invalid_consent_answer = 19U;
inline constexpr std::uint32_t invalid_credential_class = 20U;
inline constexpr std::uint32_t invalid_credential_tag = 21U;
inline constexpr std::uint32_t unlock_time_reversed = 22U;
inline constexpr std::uint32_t invalid_erasure_threshold = 23U;
inline constexpr std::uint32_t invalid_credential_length = 24U;
inline constexpr std::uint32_t duress_credential_too_similar = 25U;
inline constexpr std::uint32_t invalid_domain_presence = 26U;
// M4.1 chrome-lease codes. Numbered from 27 rather than 15: this branch was
// cut before M4.2-M4.10 claimed 15-26, and a shell error code is a wire-
// visible value, so reusing one would silently change what an existing
// response means rather than merely renaming it.
inline constexpr std::uint32_t invalid_chrome_lease = 27U;
inline constexpr std::uint32_t stale_chrome_lease = 28U;
inline constexpr std::uint32_t chrome_authority_denied = 29U;
} // namespace errors

[[nodiscard]] constexpr os::core::Error shell_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::shell, code);
}

} // namespace os::shell
