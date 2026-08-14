#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <os/kernel/aarch64_exception.hpp>

namespace {
void require(bool condition) { if (!condition) std::abort(); }

// Builds a syndrome the way hardware would: exception class in ESR[31:26],
// IL set (every AArch64 instruction is 32-bit), ISS in the low bits.
constexpr std::uint64_t esr_for(std::uint8_t exception_class, std::uint32_t iss) {
    return (static_cast<std::uint64_t>(exception_class)
                << os::kernel::aarch64::esr_exception_class_shift) |
           os::kernel::aarch64::esr_instruction_length_bit |
           static_cast<std::uint64_t>(iss);
}
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

    // Fault classification. The decoder only ever runs when something has
    // already gone wrong, so it is exactly the code least likely to be
    // exercised by a working system and most costly to have wrong - hence
    // checking it here against hand-built syndromes rather than waiting for a
    // real fault to disagree with it.
    // The exact fault the M7.11 work first hit: a level-3 translation fault on
    // a write, taken at EL1 against an address the early identity map did not
    // cover. Previously this printed "EXCEPTION" and nothing else.
    {
        const auto fault = describe_fault(
            esr_for(exception_class_data_abort_current, 0x07U | abort_iss_write_bit));
        require(fault.kind == FaultKind::data_abort);
        require(fault.cause == AbortCause::translation);
        require(fault.level == 3U);
        require(fault.level_meaningful());
        require(fault.write);
        require(!fault.from_lower_el);
        require(fault.far_valid);
    }

    // A read permission fault from EL0 - the shape a user process gets for
    // writing a read-only page, and the one a pager must be able to tell from
    // a translation fault, since only one of them is resolvable by mapping.
    {
        const auto fault = describe_fault(
            esr_for(exception_class_data_abort_lower, 0x0DU));
        require(fault.kind == FaultKind::data_abort);
        require(fault.cause == AbortCause::permission);
        require(fault.level == 1U);
        require(!fault.write);
        require(fault.from_lower_el);
    }

    // FnV set means FAR holds nothing useful; a reporter must not print it.
    {
        const auto fault = describe_fault(esr_for(
            exception_class_data_abort_current, 0x10U | abort_iss_far_not_valid_bit));
        require(fault.cause == AbortCause::external);
        require(!fault.far_valid);
        require(!fault.level_meaningful());
    }

    // Instruction aborts carry no WnR bit; write must not be inferred from a
    // reserved bit position that happens to be set.
    {
        const auto fault = describe_fault(
            esr_for(exception_class_instruction_abort_lower, 0x05U | abort_iss_write_bit));
        require(fault.kind == FaultKind::instruction_abort);
        require(fault.cause == AbortCause::translation);
        require(fault.level == 1U);
        require(!fault.write);
    }

    // Alignment and TLB-conflict encodings sit outside the level-bearing
    // group and must not be reported with a level.
    {
        const auto alignment = describe_fault(
            esr_for(exception_class_data_abort_current, 0x21U));
        require(alignment.cause == AbortCause::alignment);
        require(!alignment.level_meaningful());
        const auto conflict = describe_fault(
            esr_for(exception_class_data_abort_current, 0x30U));
        require(conflict.cause == AbortCause::tlb_conflict);
        require(!conflict.level_meaningful());
    }

    // Cookie does not enable application FP/SIMD state, so this trap is a real
    // condition rather than a theoretical one - it means something touched
    // V0-V31 despite the build-time check on the boot image's disassembly.
    {
        const auto fault = describe_fault(esr_for(exception_class_simd_fp_trap, 0U));
        require(fault.kind == FaultKind::simd_trap);
        require(fault.cause == AbortCause::unknown);
    }

    // A syscall is not a fault. describe_fault must not classify the one
    // synchronous exception the kernel actually serves as a failure.
    {
        const auto fault = describe_fault(esr_for(exception_class_svc_aarch64, 0U));
        require(fault.kind == FaultKind::unknown);
    }

    return EXIT_SUCCESS;
}
