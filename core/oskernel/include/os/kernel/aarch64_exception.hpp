#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace os::kernel::aarch64 {

// AArch64 exception vector tables contain 16 architecturally selected entries,
// each occupying 128 bytes. VBAR_EL1 therefore names a 2048-byte-aligned table.
// Keep these constants in C++ as well as assembly so the linker/vector layout can
// be checked rather than trusted as a comment in a .S file.
inline constexpr std::size_t exception_vector_entry_bytes = 128U;
inline constexpr std::size_t exception_vector_entry_count = 16U;
inline constexpr std::size_t exception_vector_table_bytes =
    exception_vector_entry_bytes * exception_vector_entry_count;
inline constexpr std::size_t exception_vector_table_alignment = 2048U;

static_assert(exception_vector_table_bytes == 2048U);

// Canonical integer exception frame shared by vector entry, syscall dispatch and
// scheduler/context-switch code. The portable kernel does not inspect this
// structure; only the AArch64 machine layer may do so.
//
// SIMD/FP state is intentionally not part of this first frame. Cookie does not
// enable application FP/SIMD context until a separately reviewed lazy/eager save
// policy exists. Silently letting userspace touch V0-V31 without preserving them
// would create cross-thread information leakage.
struct alignas(16) ExceptionFrame final {
    std::array<std::uint64_t, 31U> x {};
    std::uint64_t sp_el0 {0U};
    std::uint64_t elr_el1 {0U};
    std::uint64_t spsr_el1 {0U};
    std::uint64_t esr_el1 {0U};
    std::uint64_t far_el1 {0U};
};

static_assert(alignof(ExceptionFrame) == 16U);
static_assert(sizeof(ExceptionFrame) % 16U == 0U);

inline constexpr std::uint64_t esr_exception_class_shift = 26U;
inline constexpr std::uint64_t esr_exception_class_mask = 0x3FULL;
inline constexpr std::uint64_t esr_instruction_length_bit = 1ULL << 25U;
inline constexpr std::uint64_t esr_iss_mask = 0x01FF'FFFFULL;
inline constexpr std::uint8_t exception_class_svc_aarch64 = 0x15U;

struct ExceptionSyndrome final {
    std::uint8_t exception_class {0U};
    bool instruction_is_32bit {false};
    std::uint32_t iss {0U};
};

[[nodiscard]] constexpr ExceptionSyndrome decode_exception_syndrome(
    std::uint64_t esr) noexcept {
    return ExceptionSyndrome{
        .exception_class = static_cast<std::uint8_t>(
            (esr >> esr_exception_class_shift) & esr_exception_class_mask),
        .instruction_is_32bit = (esr & esr_instruction_length_bit) != 0U,
        .iss = static_cast<std::uint32_t>(esr & esr_iss_mask),
    };
}

[[nodiscard]] constexpr bool is_aarch64_svc(const ExceptionSyndrome& syndrome) noexcept {
    return syndrome.exception_class == exception_class_svc_aarch64 &&
        syndrome.instruction_is_32bit;
}

// For an AArch64 SVC exception, ISS[15:0] contains the immediate encoded by the
// SVC instruction. Cookie currently reserves the immediate as zero and carries
// the fixed syscall number in x8; keeping the decoder explicit lets vector entry
// reject unexpected trap encodings rather than treating every synchronous fault
// as a system call.
[[nodiscard]] constexpr std::uint16_t svc_immediate(
    const ExceptionSyndrome& syndrome) noexcept {
    return static_cast<std::uint16_t>(syndrome.iss & 0xFFFFU);
}

[[nodiscard]] constexpr bool valid_cookie_svc(const ExceptionSyndrome& syndrome) noexcept {
    return is_aarch64_svc(syndrome) && svc_immediate(syndrome) == 0U;
}

// Everything below classifies the synchronous exceptions that are *not* a
// system call. Until this existed the kernel had one word for all of them -
// "EXCEPTION" - which is the same diagnostic poverty the named halt stages in
// aarch64_boot.cpp were introduced to fix, and for the same reason: a fault
// that cannot say what it was costs hours per occurrence, because the only
// remaining evidence is a QEMU instruction trace.
//
// This is pure decoding of architected fields. It makes no decision, reads no
// state, and is therefore host-testable, which matters for the one part of
// the kernel that by definition only runs when something has already gone
// wrong. M7.11 will route the same description to a userland pager rather
// than to a panic; the classification does not change when it does.

inline constexpr std::uint8_t exception_class_simd_fp_trap = 0x07U;
inline constexpr std::uint8_t exception_class_illegal_state = 0x0EU;
inline constexpr std::uint8_t exception_class_instruction_abort_lower = 0x20U;
inline constexpr std::uint8_t exception_class_instruction_abort_current = 0x21U;
inline constexpr std::uint8_t exception_class_pc_alignment = 0x22U;
inline constexpr std::uint8_t exception_class_data_abort_lower = 0x24U;
inline constexpr std::uint8_t exception_class_data_abort_current = 0x25U;
inline constexpr std::uint8_t exception_class_sp_alignment = 0x26U;
inline constexpr std::uint8_t exception_class_serror = 0x2FU;

// Abort ISS fields. WnR and FnV are only architecturally meaningful for a
// data abort; describe_fault reports them as false for everything else rather
// than leaving a caller to remember that.
inline constexpr std::uint32_t abort_iss_status_mask = 0x3FU;
inline constexpr std::uint32_t abort_iss_write_bit = 1U << 6U;
inline constexpr std::uint32_t abort_iss_far_not_valid_bit = 1U << 10U;

enum class FaultKind : std::uint8_t {
    unknown = 0U,
    data_abort = 1U,
    instruction_abort = 2U,
    pc_alignment = 3U,
    sp_alignment = 4U,
    illegal_state = 5U,
    simd_trap = 6U,
    serror = 7U,
};

// The fault status encodings that carry a translation-table level in their low
// two bits are grouped; the rest stand alone. Reporting "translation fault at
// level 3" rather than "status 0x07" is the whole point, because the level
// says which table was missing an entry.
enum class AbortCause : std::uint8_t {
    unknown = 0U,
    address_size = 1U,
    translation = 2U,
    access_flag = 3U,
    permission = 4U,
    external = 5U,
    alignment = 6U,
    tlb_conflict = 7U,
};

struct FaultDescription final {
    FaultKind kind {FaultKind::unknown};
    AbortCause cause {AbortCause::unknown};
    std::uint8_t level {0U};
    bool from_lower_el {false};
    bool write {false};
    bool far_valid {false};

    // True when `level` was decoded from the status field rather than left at
    // its default, so a reporter can omit a level that means nothing.
    [[nodiscard]] constexpr bool level_meaningful() const noexcept {
        return cause == AbortCause::address_size || cause == AbortCause::translation ||
               cause == AbortCause::access_flag || cause == AbortCause::permission;
    }
};

[[nodiscard]] constexpr AbortCause decode_abort_cause(std::uint32_t iss) noexcept {
    switch (iss & abort_iss_status_mask & 0x3CU) {
    case 0x00U: return AbortCause::address_size;
    case 0x04U: return AbortCause::translation;
    case 0x08U: return AbortCause::access_flag;
    case 0x0CU: return AbortCause::permission;
    default: break;
    }
    switch (iss & abort_iss_status_mask) {
    case 0x10U: return AbortCause::external;
    case 0x21U: return AbortCause::alignment;
    case 0x30U: return AbortCause::tlb_conflict;
    default: return AbortCause::unknown;
    }
}

[[nodiscard]] constexpr FaultDescription describe_fault(std::uint64_t esr) noexcept {
    const auto syndrome = decode_exception_syndrome(esr);
    FaultDescription described{};
    switch (syndrome.exception_class) {
    case exception_class_data_abort_lower:
    case exception_class_data_abort_current:
        described.kind = FaultKind::data_abort;
        described.from_lower_el =
            syndrome.exception_class == exception_class_data_abort_lower;
        described.cause = decode_abort_cause(syndrome.iss);
        described.level = static_cast<std::uint8_t>(syndrome.iss & 0x3U);
        described.write = (syndrome.iss & abort_iss_write_bit) != 0U;
        described.far_valid = (syndrome.iss & abort_iss_far_not_valid_bit) == 0U;
        break;
    case exception_class_instruction_abort_lower:
    case exception_class_instruction_abort_current:
        described.kind = FaultKind::instruction_abort;
        described.from_lower_el =
            syndrome.exception_class == exception_class_instruction_abort_lower;
        described.cause = decode_abort_cause(syndrome.iss);
        described.level = static_cast<std::uint8_t>(syndrome.iss & 0x3U);
        described.far_valid = (syndrome.iss & abort_iss_far_not_valid_bit) == 0U;
        break;
    case exception_class_pc_alignment:
        described.kind = FaultKind::pc_alignment;
        described.far_valid = true;
        break;
    case exception_class_sp_alignment: described.kind = FaultKind::sp_alignment; break;
    case exception_class_illegal_state: described.kind = FaultKind::illegal_state; break;
    // Cookie deliberately does not enable application FP/SIMD context - see
    // ExceptionFrame's own comment. This trap therefore means kernel or user
    // code touched V0-V31 anyway, which the boot image's CI already greps the
    // disassembly for. Naming it separately turns a build-time check into a
    // runtime one that reports the same thing.
    case exception_class_simd_fp_trap: described.kind = FaultKind::simd_trap; break;
    case exception_class_serror: described.kind = FaultKind::serror; break;
    default: break;
    }
    return described;
}

} // namespace os::kernel::aarch64
