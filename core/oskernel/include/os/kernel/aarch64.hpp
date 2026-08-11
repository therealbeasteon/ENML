#pragma once

#include <cstdint>
#include <limits>

#include <os/core/error.hpp>
#include <os/core/result.hpp>

namespace os::kernel::aarch64 {

inline constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000ULL;
inline constexpr std::uint64_t architectural_page_size = 4096ULL;

namespace errors {
inline constexpr std::uint32_t invalid_counter_frequency = 20U;
inline constexpr std::uint32_t timer_out_of_range = 21U;
inline constexpr std::uint32_t invalid_boot_exception_level = 22U;
} // namespace errors

enum class ExceptionLevel : std::uint8_t {
    el1 = 1U,
    el2 = 2U,
    el3 = 3U,
};

[[nodiscard]] constexpr os::core::Error error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

// CurrentEL carries the exception level in bits [3:2]. A kernel handoff at EL0
// is not meaningful, and nonzero reserved bits indicate that the value did not
// come from the architectural register contract we expect.
[[nodiscard]] inline os::core::Result<ExceptionLevel>
decode_boot_exception_level(std::uint64_t current_el) noexcept {
    if ((current_el & ~0xCULL) != 0ULL) {
        return error(errors::invalid_boot_exception_level);
    }
    switch ((current_el >> 2U) & 0x3ULL) {
    case 1ULL: return ExceptionLevel::el1;
    case 2ULL: return ExceptionLevel::el2;
    case 3ULL: return ExceptionLevel::el3;
    default: return error(errors::invalid_boot_exception_level);
    }
}

// CNTFRQ_EL0 exposes a 32-bit counter frequency. Keeping that width in the
// contract matters: it gives the conversion a simple, reviewable overflow proof
// instead of pretending an arbitrary uint64 frequency is architectural input.
[[nodiscard]] inline os::core::Result<std::uint64_t> nanoseconds_to_ticks(
    std::uint64_t nanoseconds,
    std::uint32_t frequency_hz) noexcept {
    if (frequency_hz == 0U) {
        return error(errors::invalid_counter_frequency);
    }

    const std::uint64_t frequency = static_cast<std::uint64_t>(frequency_hz);
    const std::uint64_t whole_seconds = nanoseconds / nanoseconds_per_second;
    const std::uint64_t remainder_ns = nanoseconds % nanoseconds_per_second;

    if (whole_seconds > (std::numeric_limits<std::uint64_t>::max() / frequency)) {
        return error(errors::timer_out_of_range);
    }
    const std::uint64_t whole_ticks = whole_seconds * frequency;

    // remainder_ns < 1e9 and frequency <= UINT32_MAX, so the product is below
    // 2^64 by construction.
    const std::uint64_t fractional_ticks =
        (remainder_ns * frequency) / nanoseconds_per_second;
    if (whole_ticks > (std::numeric_limits<std::uint64_t>::max() - fractional_ticks)) {
        return error(errors::timer_out_of_range);
    }
    return whole_ticks + fractional_ticks;
}

// Monotonic-time reporting cannot return an error through machine.hpp. Saturate
// on representational overflow instead of wrapping backwards, preserving the
// contract that observed machine time never decreases.
[[nodiscard]] inline std::uint64_t ticks_to_nanoseconds_saturating(
    std::uint64_t ticks,
    std::uint32_t frequency_hz) noexcept {
    if (frequency_hz == 0U) return 0ULL;

    const std::uint64_t frequency = static_cast<std::uint64_t>(frequency_hz);
    const std::uint64_t whole_seconds = ticks / frequency;
    const std::uint64_t remainder_ticks = ticks % frequency;
    if (whole_seconds >
        (std::numeric_limits<std::uint64_t>::max() / nanoseconds_per_second)) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    const std::uint64_t whole_ns = whole_seconds * nanoseconds_per_second;

    // remainder_ticks < frequency <= UINT32_MAX, so this product is bounded.
    const std::uint64_t fractional_ns =
        (remainder_ticks * nanoseconds_per_second) / frequency;
    if (whole_ns > (std::numeric_limits<std::uint64_t>::max() - fractional_ns)) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return whole_ns + fractional_ns;
}

} // namespace os::kernel::aarch64
