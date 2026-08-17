#include <os/abi/syscall.hpp>

// The trap, and nothing else.
//
// Everything that decides anything - which registers a call occupies, what an
// outcome means, whether an answer arrived - is in syscall.cpp and is
// architecture-neutral, so the whole contract is provable in a host test. This
// file is the twenty instructions that cannot be.
//
// That split is worth more here than it usually would be. `kernel-arm64-native`
// is this project's only real verification and costs a QEMU boot per attempt;
// a contract that can be held without one should be.
#if defined(__aarch64__)

namespace os::abi {

os::core::Result<SyscallAnswer> trap(const SyscallRequest& request) noexcept {
    register std::uint64_t r0 asm("x0") = request.arguments[0];
    register std::uint64_t r1 asm("x1") = request.arguments[1];
    register std::uint64_t r2 asm("x2") = request.arguments[2];
    register std::uint64_t r3 asm("x3") = request.arguments[3];
    // Zeroed before the trap, and this line is the reason the outcome tag is
    // worth having at all. A kernel path that returns without writing x7 leaves
    // this zero, which decodes as "no answer". Without it the register would
    // hold whatever the *previous* call left there, and a previous success
    // would make a silently undispatched call look like a fresh one.
    register std::uint64_t r7 asm("x7") = 0ULL;
    register std::uint64_t r8 asm("x8") = static_cast<std::uint64_t>(request.call);

    // "memory" because a send publishes the bytes at the exchange address to
    // the kernel and a receive reads bytes the kernel wrote there. Without it
    // the compiler may sink a store past the trap or hoist a load before it,
    // and the message would be whatever was there instead.
    asm volatile("svc #0"
                 : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3), "+r"(r7)
                 : "r"(r8)
                 : "memory", "cc");

    return decode_outcome(r7, {r0, r1, r2, r3});
}

} // namespace os::abi

#endif // __aarch64__
