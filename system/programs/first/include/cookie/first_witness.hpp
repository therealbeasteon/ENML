#pragma once

#include <cstdint>

// The witness the first compiled program carries, and the one definition of how
// it is computed.
//
// docs/M7_16_FIRST_PROGRAM_CONTRACT.md requires that a compiled program prove it
// ran by "a kernel-observed syscall effect... something a constant could not have
// produced", and records why the x19 marker the hand-written programs use cannot
// serve: x19 is callee-saved, so its value at the instant of the `svc` is not the
// program's to guarantee once a function call stands between the two.
//
// So the witness rides in an argument register the stub writes and the ABI
// defines, and it is *folded at run time* rather than assembled by the compiler
// into a literal. `witness_seed` is read through a volatile lvalue in the
// program, which is what forces the fold to happen on the machine instead of in
// the compiler - a program that emitted the answer as one `movz` would prove
// nothing a hand-written word could not.
//
// The kernel computes the same value from this same header at build time, so
// there is one definition of the algorithm and no second opinion to drift from.
// That is the rule .ckx's construction cost and M7.16's link address already
// follow.
namespace cookie::first {

// Arbitrary, and its only requirements are that it is non-zero and that nothing
// else in the ABI means anything by it. Non-zero because the value is passed
// where a capability id goes and `decode_address_space_destroy_syscall` refuses
// zero - the witness has to survive decoding to be observed.
inline constexpr std::uint64_t witness_seed = 0xC00C'1E00'F123'0001ULL;

// A fold, not a hash. It needs to be cheap, exactly reproducible on both sides,
// and impossible for a compiler to reduce to a constant when its input is
// volatile - not cryptographic. Eight rounds because the point is to require
// arithmetic, and eight is enough instructions that no single-instruction
// materialisation of the result exists.
inline constexpr std::uint64_t fold_witness(std::uint64_t seed) noexcept {
    std::uint64_t accumulator = seed;
    for (std::uint64_t round = 1U; round <= 8U; ++round) {
        accumulator = (accumulator * 0x0000'0100'0000'01B3ULL) ^ round;
    }
    // The low bit is forced so the value can never be zero, whatever the fold
    // produces. A zero witness would be refused at decode and the proof would
    // report "the program did not run" for a program that ran perfectly.
    return accumulator | 1ULL;
}

// The value the kernel expects to see. constexpr on this side deliberately: the
// checker should cost nothing at run time, and the *program* is the side that
// must compute it.
inline constexpr std::uint64_t expected_witness = fold_witness(witness_seed);

static_assert(expected_witness != 0ULL);
static_assert(expected_witness != witness_seed,
              "the fold must transform its input, or the program could pass the "
              "seed straight through and prove nothing");

// Distinct value for the decoy at the base of the code region.
//
// The decoy exists so that a kernel entering the mapping at its base rather than
// at the entry the space's sealed root declares is caught instead of passing
// silently - the construction M7.12's boot proof introduced and this program
// keeps. It folds a different seed, so the two are told apart by the same
// arithmetic rather than by a flag.
inline constexpr std::uint64_t decoy_seed = 0xC00C'1E00'DEC0'0001ULL;
inline constexpr std::uint64_t expected_decoy_witness = fold_witness(decoy_seed);

static_assert(expected_decoy_witness != expected_witness);

} // namespace cookie::first
