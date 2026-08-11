#include <cstdint>
#include <cstdio>

#include <os/core/error.hpp>
#include <os/kernel/abi.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "kernel abi: %s\n", what);
    }
    return condition;
}

} // namespace

// These tests guard a size, not a behaviour.
//
// The security argument for writing a kernel is that it can be small enough to
// review completely. Nothing enforces that except a test that fails when the
// surface grows, so this file is the enforcement.
int main() {
    // The ceiling. Reaching it means removing a call or moving it into a
    // service; raising the number is the failure this exists to catch.
    if (!check(os::kernel::kernel_call_count() <= os::kernel::max_kernel_calls,
               "system call surface exceeded its ceiling")) return 1;

    // For reference: QNX ran a whole operating system on fourteen calls. If
    // this number climbs well past that, the design has drifted rather than
    // grown.
    std::fprintf(
        stderr,
        "kernel abi: %zu calls of a permitted %zu\n",
        os::kernel::kernel_call_count(),
        os::kernel::max_kernel_calls);

    // Every entry is reachable, self-consistent, and within the argument bound.
    // A call needing more arguments than this is carrying a structure, and a
    // structure belongs in a message.
    for (std::size_t index = 0U; index < os::kernel::kernel_call_count(); ++index) {
        auto entry = os::kernel::call_at(index);
        if (!check(static_cast<bool>(entry), "table entry unreachable")) return 1;
        const auto descriptor = entry.value();

        if (!check(descriptor.argument_count <= os::kernel::max_call_arguments,
                   "call takes more arguments than the ABI permits")) return 1;
        if (!check(static_cast<std::uint16_t>(descriptor.call) != 0U,
                   "zero is not a valid call")) return 1;

        // describe_call and the table must agree; two sources of truth for the
        // kernel entry path is one too many.
        auto described = os::kernel::describe_call(descriptor.call);
        if (!check(static_cast<bool>(described), "described call missing from the table")) return 1;
        if (!check(described.value() == descriptor, "table and lookup disagree")) return 1;

        // And the number a caller supplies must decode to the same entry.
        auto decoded = os::kernel::decode_call(static_cast<std::uint16_t>(descriptor.call));
        if (!check(static_cast<bool>(decoded), "valid call number rejected")) return 1;
        if (!check(decoded.value() == descriptor, "decode disagreed with the table")) return 1;
    }

    // No duplicate call numbers. A repeated number would mean the entry path
    // resolves to whichever entry is found first, which is not a decision
    // anybody made.
    for (std::size_t left = 0U; left < os::kernel::kernel_call_count(); ++left) {
        for (std::size_t right = left + 1U; right < os::kernel::kernel_call_count(); ++right) {
            const auto a = os::kernel::call_at(left).value();
            const auto b = os::kernel::call_at(right).value();
            if (!check(a.call != b.call, "duplicate call number in the surface")) return 1;
        }
    }

    // The unprivileged core is exactly the set a thread cannot be confined by
    // losing: the communication primitives and the ability to stop. Anything
    // else being unprivileged is ambient authority, which this ABI does not
    // have.
    {
        std::size_t unprivileged = 0U;
        for (std::size_t index = 0U; index < os::kernel::kernel_call_count(); ++index) {
            const auto descriptor = os::kernel::call_at(index).value();
            if (descriptor.authority != os::kernel::CallAuthority::unprivileged) continue;
            ++unprivileged;
            const bool expected =
                descriptor.call == os::kernel::KernelCall::send ||
                descriptor.call == os::kernel::KernelCall::receive ||
                descriptor.call == os::kernel::KernelCall::reply ||
                descriptor.call == os::kernel::KernelCall::yield ||
                descriptor.call == os::kernel::KernelCall::thread_exit;
            if (!check(expected, "a call outside the core is unprivileged")) return 1;
        }
        if (!check(unprivileged == 5U, "the unprivileged core changed size")) return 1;
    }

    // Blocking is stated rather than discovered, because every blocking call is
    // somewhere a priority inversion can be built. Only the two halves of a
    // rendezvous may block: a server that could be stalled by a client
    // refusing to collect its answer would be a denial of service with no
    // defence, so reply must not.
    for (std::size_t index = 0U; index < os::kernel::kernel_call_count(); ++index) {
        const auto descriptor = os::kernel::call_at(index).value();
        if (!descriptor.blocking) continue;
        const bool permitted =
            descriptor.call == os::kernel::KernelCall::send ||
            descriptor.call == os::kernel::KernelCall::receive;
        if (!check(permitted, "a call outside the rendezvous may block")) return 1;
    }

    // Unknown call numbers are rejected rather than clamped. This is the one
    // path in the system where an attacker picks a number that selects code.
    const std::uint16_t rejected[]{0U, 16U, 100U, 0x7FFFU, 0xFFFFU};
    for (const auto raw : rejected) {
        auto decoded = os::kernel::decode_call(raw);
        if (!check(!decoded, "unknown call number accepted")) return 1;
        if (!check(decoded.error().domain == os::core::ErrorDomain::kernel,
                   "wrong error domain for an unknown call")) return 1;
    }

    // Enumerating past the end fails rather than reading past it.
    if (!check(!os::kernel::call_at(os::kernel::kernel_call_count()),
               "index past the surface accepted")) return 1;

    return 0;
}
