#include <cstdint>
#include <cstdio>

#include <os/core/error.hpp>
#include <os/kernel/abi.hpp>
#include <os/kernel/ipc_continuation.hpp>
#include <os/kernel/ipc_syscall.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "kernel abi: %s\n", what);
    }
    return condition;
}

} // namespace

int main() {
    if (!check(os::kernel::kernel_call_count() <= os::kernel::max_kernel_calls,
               "system call surface exceeded its ceiling")) return 1;

    std::fprintf(
        stderr,
        "kernel abi: %zu calls of a permitted %zu\n",
        os::kernel::kernel_call_count(),
        os::kernel::max_kernel_calls);

    for (std::size_t index = 0U; index < os::kernel::kernel_call_count(); ++index) {
        auto entry = os::kernel::call_at(index);
        if (!check(static_cast<bool>(entry), "table entry unreachable")) return 1;
        const auto descriptor = entry.value();

        if (!check(descriptor.argument_count <= os::kernel::max_call_arguments,
                   "call takes more arguments than the ABI permits")) return 1;
        if (!check(static_cast<std::uint16_t>(descriptor.call) != 0U,
                   "zero is not a valid call")) return 1;

        auto described = os::kernel::describe_call(descriptor.call);
        if (!check(static_cast<bool>(described), "described call missing from the table")) return 1;
        if (!check(described.value() == descriptor, "table and lookup disagree")) return 1;

        auto decoded = os::kernel::decode_call(static_cast<std::uint16_t>(descriptor.call));
        if (!check(static_cast<bool>(decoded), "valid call number rejected")) return 1;
        if (!check(decoded.value() == descriptor, "decode disagreed with the table")) return 1;
    }

    for (std::size_t left = 0U; left < os::kernel::kernel_call_count(); ++left) {
        for (std::size_t right = left + 1U; right < os::kernel::kernel_call_count(); ++right) {
            const auto a = os::kernel::call_at(left).value();
            const auto b = os::kernel::call_at(right).value();
            if (!check(a.call != b.call, "duplicate call number in the surface")) return 1;
        }
    }

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

    for (std::size_t index = 0U; index < os::kernel::kernel_call_count(); ++index) {
        const auto descriptor = os::kernel::call_at(index).value();
        if (!descriptor.blocking) continue;
        const bool permitted =
            descriptor.call == os::kernel::KernelCall::send ||
            descriptor.call == os::kernel::KernelCall::receive;
        if (!check(permitted, "a call outside the rendezvous may block")) return 1;
    }

    // 16 was here until fault_supply took it. 17 is the first free number now,
    // and the table is full - see max_kernel_calls.
    const std::uint16_t rejected[]{0U, 17U, 100U, 0x7FFFU, 0xFFFFU};
    for (const auto raw : rejected) {
        auto decoded = os::kernel::decode_call(raw);
        if (!check(!decoded, "unknown call number accepted")) return 1;
        if (!check(decoded.error().domain == os::core::ErrorDomain::kernel,
                   "wrong error domain for an unknown call")) return 1;
    }

    if (!check(!os::kernel::call_at(os::kernel::kernel_call_count()),
               "index past the surface accepted")) return 1;

    {
        auto send = os::kernel::decode_ipc_send_syscall(7U, 0x1000U, 64U);
        if (!check(static_cast<bool>(send), "valid IPC send registers rejected")) return 1;
        if (!check(send.value().request.length == 64U, "IPC send length changed")) return 1;
        if (!check(!os::kernel::decode_ipc_send_syscall(0U, 0x1000U, 1U),
                   "zero IPC send capability accepted")) return 1;
        if (!check(!os::kernel::decode_ipc_send_syscall(7U, 0U, 1U),
                   "null IPC send frame accepted")) return 1;
        if (!check(!os::kernel::decode_ipc_send_syscall(
                       7U, 0x1000U, os::kernel::max_ipc_inline_bytes + 1U),
                   "oversized IPC send accepted")) return 1;

        auto receive = os::kernel::decode_ipc_receive_syscall(9U, 0x2000U, 0U);
        if (!check(static_cast<bool>(receive), "valid IPC receive registers rejected")) return 1;
        if (!check(!receive.value().bounded(),
                   "zero deadline treated as bounded")) return 1;
        if (!check(!os::kernel::decode_ipc_receive_syscall(0U, 0x2000U, 0U),
                   "zero IPC receive capability accepted")) return 1;
        if (!check(!os::kernel::decode_ipc_receive_syscall(9U, 0U, 0U),
                   "null IPC receive frame accepted")) return 1;

        // Every non-zero register value is a legal relative deadline. There is
        // no negative timeout and no absolute value already in the past, so the
        // decoder has nothing to reject - including the extremes, which a
        // hostile caller will try first.
        auto bounded = os::kernel::decode_ipc_receive_syscall(9U, 0x2000U, 1U);
        if (!check(static_cast<bool>(bounded), "1ns deadline rejected")) return 1;
        if (!check(bounded.value().bounded(), "1ns deadline not bounded")) return 1;
        if (!check(bounded.value().deadline_nanoseconds == 1U,
                   "deadline not carried through decode")) return 1;
        auto huge = os::kernel::decode_ipc_receive_syscall(
            9U, 0x2000U, UINT64_MAX);
        if (!check(static_cast<bool>(huge), "maximal deadline rejected")) return 1;
        if (!check(huge.value().bounded(), "maximal deadline not bounded")) return 1;

        auto reply = os::kernel::decode_ipc_reply_syscall(11U, 0x3000U, 64U);
        if (!check(static_cast<bool>(reply), "valid IPC reply registers rejected")) return 1;
        if (!check(!os::kernel::decode_ipc_reply_syscall(0U, 0x3000U, 1U),
                   "zero IPC transaction accepted")) return 1;
        if (!check(!os::kernel::decode_ipc_reply_syscall(11U, 0U, 1U),
                   "null IPC reply frame accepted")) return 1;
        if (!check(!os::kernel::decode_ipc_reply_syscall(
                       11U, 0x3000U, os::kernel::max_ipc_inline_bytes + 1U),
                   "oversized IPC reply accepted")) return 1;
    }

    // A blocked send's eventual write-back destination is generation-bound.
    // Reusing the same virtual address after the old epoch retires does not make
    // the stale continuation valid for the replacement address space.
    {
        os::kernel::AddressSpaceEpochAuthority epochs{};
        auto first = epochs.acquire();
        if (!check(static_cast<bool>(first), "failed to acquire IPC continuation epoch")) return 1;
        os::kernel::IpcContinuationTable continuations{};
        if (!check(static_cast<bool>(continuations.arm(3U, first.value(), 0x4000U, epochs)),
                   "failed to arm IPC continuation")) return 1;
        if (!check(!continuations.arm(3U, first.value(), 0x5000U, epochs),
                   "duplicate IPC continuation accepted")) return 1;

        auto retiring = epochs.begin_retire(first.value());
        if (!check(static_cast<bool>(retiring), "failed to retire IPC continuation epoch")) return 1;
        if (!check(!continuations.take(3U, first.value(), epochs),
                   "retired IPC continuation remained usable")) return 1;
        if (!check(continuations.count() == 0U,
                   "stale IPC continuation was not consumed")) return 1;
        if (!check(static_cast<bool>(epochs.complete_retire(retiring.value())),
                   "failed to complete IPC continuation epoch retirement")) return 1;

        auto replacement = epochs.acquire();
        if (!check(static_cast<bool>(replacement), "failed to acquire replacement epoch")) return 1;
        if (!check(static_cast<bool>(continuations.arm(
                       3U, replacement.value(), 0x4000U, epochs)),
                   "replacement IPC continuation rejected")) return 1;
        auto live = continuations.take(3U, replacement.value(), epochs);
        if (!check(static_cast<bool>(live), "live IPC continuation rejected")) return 1;
        if (!check(live.value().exchange_address == 0x4000U,
                   "IPC continuation exchange address changed")) return 1;
    }

    return 0;
}
