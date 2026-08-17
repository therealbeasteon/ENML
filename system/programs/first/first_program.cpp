// Cookie's first compiled program.
//
// Everything that has run at EL0 on Cookie until now has been `uint32_t`
// instruction words written into a page by `aarch64_boot.cpp`. This is a C++
// translation unit compiled by this repository's toolchain, and it reaches the
// kernel through `core/osabi` rather than through hand-assembled `svc`.
//
// It is deliberately tiny and deliberately not a demonstration. Its whole job is
// to be the first thing whose presence at EL0 was produced by a compiler, and to
// prove that in a way a hand-written word could not fake - see
// docs/M7_16_FIRST_PROGRAM_CONTRACT.md and cookie/first_witness.hpp.
//
// What it does not have, because the contract says so: no argument vector, no
// environment, no auxiliary vector, no knowledge of its own layout, and no
// device it could print to. Its only observable effect is a system call.

#include <cstdint>

#include <cookie/first_witness.hpp>
#include <os/abi/syscall.hpp>
#include <os/kernel/address_space_syscall.hpp>

namespace {

// Nothing here may return. A program's entry has no caller to return to, and
// falling off the end of one would execute whatever the linker put next.
[[noreturn]] void rest() noexcept {
    for (;;) {
        asm volatile("nop");
    }
}

// The fold, run on the machine.
//
// `seed` is volatile so the compiler must load it rather than fold the whole
// expression into a literal. That is the property the proof rests on: an
// eight-round multiply-xor chain the compiler had to emit is something a
// hand-assembled program could not have produced with one `movz`, which is what
// docs/M7_16_FIRST_PROGRAM_CONTRACT.md's exit criteria demand of the witness.
[[nodiscard]] std::uint64_t compute_witness(std::uint64_t from) noexcept {
    volatile std::uint64_t seed = from;
    std::uint64_t accumulator = seed;
    for (std::uint64_t round = 1U; round <= 8U; ++round) {
        accumulator = (accumulator * 0x0000'0100'0000'01B3ULL) ^ round;
    }
    return accumulator | 1ULL;
}

// Carries the witness to the kernel and never comes back.
//
// `address_space_destroy` is the call, and it is chosen rather than preferred.
// The witness has to ride in a register the *stub* writes, so the call must
// declare at least one argument - `yield` declares none, and the encoder zeroes
// every register past a call's declared count precisely so nothing can smuggle a
// value through one. Of the calls this image dispatches, this is the one whose
// single argument is decoded before any capability is resolved, so the kernel
// sees the witness whether or not the value names anything.
//
// The call is expected to be *refused*. That is not a failure and is the reason
// M7.15c mattered: before malformed calls became refusals, a first program
// getting this wrong halted the machine instead of being told.
[[noreturn]] void carry(std::uint64_t seed) noexcept {
    const auto witness = compute_witness(seed);
    auto request = os::abi::encode_address_space_destroy(
        os::kernel::AddressSpaceDestroySyscall{
            static_cast<os::kernel::CapabilityId>(witness)});
    if (request) {
        // The answer is deliberately discarded. A refusal is the expected
        // outcome and the program has nowhere to report either one - the kernel
        // is the only party that can observe this, which is the whole design.
        (void)os::abi::trap(request.value());
    }
    rest();
}

} // namespace

// Two entry points, and the linker script fixes where each one lands.
//
// The decoy sits at the base of the code region so that a kernel entering the
// mapping at its base rather than at the address the space's sealed root
// declares is caught rather than passing silently. It is the same construction
// M7.12's boot proof introduced with four decoy words, expressed in the same
// language as the program it guards - and it carries a different witness, so the
// two are told apart by arithmetic rather than by a flag.
// Placed by section attribute rather than by link order. Link order is a
// property of how the object files happen to be listed, and the decoy's position
// is a security property: it has to be the thing at the base of the region.
__attribute__((section(".text.decoy"), used))
extern "C" [[noreturn]] void cookie_first_decoy() noexcept {
    carry(cookie::first::decoy_seed);
}

__attribute__((section(".text.entry"), used))
extern "C" [[noreturn]] void cookie_first_entry() noexcept {
    carry(cookie::first::witness_seed);
}
