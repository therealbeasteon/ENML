#pragma once

#include <cstdint>

namespace os::core {

enum class ErrorDomain : std::uint16_t {
    core = 1,
    ipc = 2,
    service = 3,
    storage = 4,
    package = 5,
    security = 6,
    key = 7,
    display = 8,
    ui = 9,
    shell = 10,
};

struct Error final {
    ErrorDomain domain {ErrorDomain::core};
    std::uint16_t reserved {0};
    std::uint32_t code {0};

    [[nodiscard]] friend constexpr bool operator==(const Error&, const Error&) = default;
};

namespace errors::core {
inline constexpr std::uint32_t overflow = 1;
inline constexpr std::uint32_t invalid_argument = 2;
inline constexpr std::uint32_t invalid_handle = 3;
}

namespace errors::security {
inline constexpr std::uint32_t unknown_process = 1;
inline constexpr std::uint32_t credential_mismatch = 2;
inline constexpr std::uint32_t stale_process = 3;
inline constexpr std::uint32_t registry_full = 4;
inline constexpr std::uint32_t invalid_identity = 5;
inline constexpr std::uint32_t sandbox_apply_failed = 10;
inline constexpr std::uint32_t sandbox_not_supported = 11;
}

namespace errors::service {
inline constexpr std::uint32_t unknown_operation = 1;
inline constexpr std::uint32_t invalid_request = 2;
inline constexpr std::uint32_t not_supported = 3;
inline constexpr std::uint32_t access_denied = 4;
inline constexpr std::uint32_t launch_failed = 10;
inline constexpr std::uint32_t readiness_timeout = 11;
inline constexpr std::uint32_t invalid_bootstrap = 12;
inline constexpr std::uint32_t crash_loop = 13;
inline constexpr std::uint32_t not_running = 14;
}

[[nodiscard]] constexpr Error make_error(ErrorDomain domain, std::uint32_t code) noexcept {
    return Error{domain, 0, code};
}

[[nodiscard]] constexpr Error core_error(std::uint32_t code) noexcept {
    return make_error(ErrorDomain::core, code);
}

} // namespace os::core
