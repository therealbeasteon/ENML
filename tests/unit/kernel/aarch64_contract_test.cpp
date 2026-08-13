#include <cstdint>
#include <cstdlib>
#include <limits>

#include <os/kernel/aarch64.hpp>
#include <os/kernel/aarch64_exception.hpp>

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

    using namespace os::kernel::aarch64;
    require(exception_vector_entry_count == 16U);
    require(exception_vector_entry_bytes == 128U);
    require(exception_vector_table_bytes == 2048U);
    require(exception_vector_table_alignment == 2048U);
    require(sizeof(ExceptionFrame) == 288U);

    const std::uint64_t svc0_esr =
        (static_cast<std::uint64_t>(exception_class_svc_aarch64) << esr_exception_class_shift) |
        esr_instruction_length_bit;
    const auto svc0 = decode_exception_syndrome(svc0_esr);
    require(is_aarch64_svc(svc0));
    require(svc_immediate(svc0) == 0U);
    require(valid_cookie_svc(svc0));

    const auto svc_nonzero = decode_exception_syndrome(svc0_esr | 0x42U);
    require(is_aarch64_svc(svc_nonzero));
    require(svc_immediate(svc_nonzero) == 0x42U);
    require(!valid_cookie_svc(svc_nonzero));

    const auto data_abort = decode_exception_syndrome(
        (0x24ULL << esr_exception_class_shift) | esr_instruction_length_bit);
    require(!is_aarch64_svc(data_abort));
    require(!valid_cookie_svc(data_abort));

    // AArch64 SVC is a 32-bit instruction. A syndrome with the right class but
    // IL clear cannot be accepted as Cookie's syscall trap.
    const auto malformed_svc = decode_exception_syndrome(
        static_cast<std::uint64_t>(exception_class_svc_aarch64) << esr_exception_class_shift);
    require(!valid_cookie_svc(malformed_svc));

    return EXIT_SUCCESS;
}
