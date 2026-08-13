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

} // namespace os::kernel::aarch64
