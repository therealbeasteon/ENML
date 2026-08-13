#include <cstdint>
#include <cstdlib>
#include <limits>

#include <os/kernel/aarch64.hpp>

namespace {

void require(bool condition) {
    if (!condition) std::abort();
}

} // namespace

int main() {
    using os::kernel::aarch64::ExceptionLevel;

    auto el1 = os::kernel::aarch64::decode_boot_exception_level(0x4ULL);
    auto el2 = os::kernel::aarch64::decode_boot_exception_level(0x8ULL);
    auto el3 = os::kernel::aarch64::decode_boot_exception_level(0xCULL);
    require(el1 && el1.value() == ExceptionLevel::el1);
    require(el2 && el2.value() == ExceptionLevel::el2);
    require(el3 && el3.value() == ExceptionLevel::el3);

    auto el0 = os::kernel::aarch64::decode_boot_exception_level(0x0ULL);
    auto reserved = os::kernel::aarch64::decode_boot_exception_level(0x14ULL);
    require(!el0);
    require(!reserved);

    auto invalid_frequency = os::kernel::aarch64::nanoseconds_to_ticks(1ULL, 0U);
    require(!invalid_frequency);

    // Exact conversions at realistic generic-timer frequencies.
    auto one_second = os::kernel::aarch64::nanoseconds_to_ticks(1'000'000'000ULL, 62'500'000U);
    require(one_second && one_second.value() == 62'500'000ULL);

    auto half_second = os::kernel::aarch64::nanoseconds_to_ticks(500'000'000ULL, 24'000'000U);
    require(half_second && half_second.value() == 12'000'000ULL);

    // Sub-tick intervals round down rather than accidentally scheduling a huge
    // delay through unsigned underflow.
    auto sub_tick = os::kernel::aarch64::nanoseconds_to_ticks(1ULL, 1'000'000U);
    require(sub_tick && sub_tick.value() == 0ULL);

    auto too_large = os::kernel::aarch64::nanoseconds_to_ticks(
        std::numeric_limits<std::uint64_t>::max(),
        std::numeric_limits<std::uint32_t>::max());
    require(!too_large);

    require(os::kernel::aarch64::ticks_to_nanoseconds_saturating(24'000'000ULL, 24'000'000U) ==
        1'000'000'000ULL);
    require(os::kernel::aarch64::ticks_to_nanoseconds_saturating(12'000'000ULL, 24'000'000U) ==
        500'000'000ULL);
    require(os::kernel::aarch64::ticks_to_nanoseconds_saturating(100ULL, 0U) == 0ULL);

    return EXIT_SUCCESS;
}
