#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <os/kernel/aarch64_exception.hpp>

namespace {
void require(bool condition) { if (!condition) std::abort(); }
}

int main() {
    using namespace os::kernel::aarch64;

    require(exception_vector_entry_bytes == 128U);
    require(exception_vector_entry_count == 16U);
    require(exception_vector_table_bytes == 2048U);
    require(exception_vector_table_alignment == 2048U);
    require(sizeof(ExceptionFrame) == 288U);
    require(alignof(ExceptionFrame) == 16U);

    // ESR_EL1: EC=0x15 (AArch64 SVC), IL=1, ISS immediate=0.
    const std::uint64_t valid_esr =
        (static_cast<std::uint64_t>(exception_class_svc_aarch64) << esr_exception_class_shift) |
        esr_instruction_length_bit;
    const auto valid = decode_exception_syndrome(valid_esr);
    require(is_aarch64_svc(valid));
    require(valid_cookie_svc(valid));
    require(svc_immediate(valid) == 0U);

    const auto wrong_immediate = decode_exception_syndrome(valid_esr | 7ULL);
    require(is_aarch64_svc(wrong_immediate));
    require(!valid_cookie_svc(wrong_immediate));
    require(svc_immediate(wrong_immediate) == 7U);

    const auto not_svc = decode_exception_syndrome(
        (0x24ULL << esr_exception_class_shift) | esr_instruction_length_bit);
    require(!is_aarch64_svc(not_svc));
    require(!valid_cookie_svc(not_svc));

    // AArch64 instructions are 32-bit; an SVC-class syndrome without IL set is
    // not accepted as Cookie's syscall entry even if the immediate is zero.
    const auto bad_length = decode_exception_syndrome(
        static_cast<std::uint64_t>(exception_class_svc_aarch64) << esr_exception_class_shift);
    require(!valid_cookie_svc(bad_length));

    return EXIT_SUCCESS;
}
