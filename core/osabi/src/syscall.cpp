#include <os/abi/syscall.hpp>

namespace os::abi {
namespace {

// The ten calls this module encodes. Kept beside calls_without_stubs, which
// the test holds against the ABI table so the two partition it exactly.
constexpr std::array<os::kernel::KernelCall, 10U> calls_with_stubs{
    os::kernel::KernelCall::send,
    os::kernel::KernelCall::receive,
    os::kernel::KernelCall::reply,
    os::kernel::KernelCall::yield,
    os::kernel::KernelCall::thread_create,
    os::kernel::KernelCall::address_space_create,
    os::kernel::KernelCall::address_space_destroy,
    os::kernel::KernelCall::fault_supply,
    os::kernel::KernelCall::map,
    os::kernel::KernelCall::unmap,
};

static_assert(
    calls_with_stubs.size() + calls_without_stubs.size() ==
        os::kernel::max_kernel_calls,
    "the implemented and unimplemented sets must partition the ABI surface; a "
    "call in neither is one nobody decided about");

} // namespace

os::core::Result<Refusal> decode_error(std::uint64_t raw) noexcept {
    const auto reserved = static_cast<std::uint16_t>((raw >> 32U) & 0xFFFFULL);
    if (reserved != 0U) {
        // A field with no meaning must carry no content. The same rule .ckx
        // applies to its own reserved bytes, and for the same reason: a field
        // nothing reads is a field something can write.
        return os::core::Result<Refusal>{abi_error(abi_errors::malformed_error)};
    }
    return os::core::Result<Refusal>{Refusal{os::core::Error{
        static_cast<os::core::ErrorDomain>((raw >> 48U) & 0xFFFFULL),
        0U,
        static_cast<std::uint32_t>(raw & 0xFFFF'FFFFULL)}}};
}

os::core::Result<SyscallRequest> encode_call(
    os::kernel::KernelCall call,
    const std::array<std::uint64_t, argument_registers>& arguments,
    std::size_t argument_count) noexcept {
    // The descriptor is the authority on how many arguments a call takes.
    // Asking rather than restating is the whole point: a literal here would be
    // a second copy of the ABI, and the failure when two copies drift has no
    // symptom at the site of the drift.
    auto descriptor = os::kernel::describe_call(call);
    if (!descriptor) {
        return os::core::Result<SyscallRequest>{
            abi_error(abi_errors::unknown_call)};
    }
    if (argument_count != static_cast<std::size_t>(descriptor.value().argument_count)) {
        return os::core::Result<SyscallRequest>{
            abi_error(abi_errors::argument_count)};
    }

    SyscallRequest request{};
    request.call = static_cast<std::uint16_t>(call);
    for (std::size_t index = 0U; index < argument_count; ++index) {
        request.arguments[index] = arguments[index];
    }
    // Everything past the declared count is already zero from the initialiser
    // above, and that is deliberate rather than incidental - see the header.
    return os::core::Result<SyscallRequest>{request};
}

os::core::Result<SyscallAnswer> decode_outcome(
    std::uint64_t outcome,
    const std::array<std::uint64_t, argument_registers>& registers) noexcept {
    if (outcome == outcome_answered) {
        return os::core::Result<SyscallAnswer>{SyscallAnswer{registers}};
    }
    if (outcome == outcome_refused) {
        auto refusal = decode_error(registers[0]);
        if (!refusal) return os::core::Result<SyscallAnswer>{refusal.error()};
        return os::core::Result<SyscallAnswer>{refusal.value().reason};
    }
    // Neither tag. The caller zeroed x7 before the trap, so this is a kernel
    // path that returned without writing an outcome - or nothing that ran at
    // all. Not a refusal: a refusal is an answer, and this is the absence of
    // one. Collapsing the two would let a call that was never dispatched look
    // like one that was considered and declined.
    return os::core::Result<SyscallAnswer>{abi_error(abi_errors::no_answer)};
}

bool call_has_stub(os::kernel::KernelCall call) noexcept {
    for (const auto candidate : calls_with_stubs) {
        if (candidate == call) return true;
    }
    return false;
}

os::core::Result<SyscallRequest> encode_send(
    const os::kernel::IpcSendSyscall& request) noexcept {
    return encode_call(
        os::kernel::KernelCall::send,
        {static_cast<std::uint64_t>(request.endpoint_capability),
         request.request.address,
         static_cast<std::uint64_t>(request.request.length),
         0ULL},
        3U);
}

os::core::Result<SyscallRequest> encode_receive(
    const os::kernel::IpcReceiveSyscall& request) noexcept {
    // Three: capability, exchange address, relative deadline. Writing this
    // encoder is what found that the ABI table had said two since before the
    // kernel existed, while the decoder had taken three ever since receive
    // gained a deadline - see the note beside the descriptor in abi.cpp. The
    // count is checked against the table by encode_call and the deadline is
    // round-tripped through the kernel's own decoder by test, so the table, this
    // encoder and that decoder cannot drift apart again without something red.
    return encode_call(
        os::kernel::KernelCall::receive,
        {static_cast<std::uint64_t>(request.endpoint_capability),
         request.exchange_address,
         request.deadline_nanoseconds,
         0ULL},
        3U);
}

os::core::Result<SyscallRequest> encode_reply(
    const os::kernel::IpcReplySyscall& request) noexcept {
    return encode_call(
        os::kernel::KernelCall::reply,
        {static_cast<std::uint64_t>(request.transaction),
         request.response.address,
         static_cast<std::uint64_t>(request.response.length),
         0ULL},
        3U);
}

os::core::Result<SyscallRequest> encode_yield() noexcept {
    // Zero arguments, so all four registers are written zero. A call that takes
    // nothing still writes the whole set - "takes nothing" and "leaves whatever
    // was there" are different statements and only one of them is safe.
    return encode_call(os::kernel::KernelCall::yield, {0ULL, 0ULL, 0ULL, 0ULL}, 0U);
}

os::core::Result<SyscallRequest> encode_thread_create(
    const os::kernel::ThreadCreateSyscall& request) noexcept {
    // Two arguments, and the three it does not take are the design rather than
    // an omission - no entry, no thread identifier, no priority. See
    // docs/M7_12_ENTRY_BINDING.md. A stub is exactly where those would creep
    // back in as a convenience, so they are absent here too.
    return encode_call(
        os::kernel::KernelCall::thread_create,
        {static_cast<std::uint64_t>(request.space), request.stack, 0ULL, 0ULL},
        2U);
}

os::core::Result<SyscallRequest> encode_address_space_create(
    const os::kernel::AddressSpaceCreateSyscall& request) noexcept {
    return encode_call(
        os::kernel::KernelCall::address_space_create,
        {static_cast<std::uint64_t>(request.authority),
         static_cast<std::uint64_t>(request.root_grant),
         0ULL,
         0ULL},
        2U);
}

os::core::Result<SyscallRequest> encode_address_space_destroy(
    const os::kernel::AddressSpaceDestroySyscall& request) noexcept {
    return encode_call(
        os::kernel::KernelCall::address_space_destroy,
        {static_cast<std::uint64_t>(request.space), 0ULL, 0ULL, 0ULL},
        1U);
}

os::core::Result<SyscallRequest> encode_fault_supply(
    const os::kernel::FaultSupplySyscall& request) noexcept {
    // One argument, and it names the backing rather than the region. A pager
    // able to name a region could answer a question it was never asked; the
    // encoder has nowhere to put one, which is the stronger form of that rule.
    return encode_call(
        os::kernel::KernelCall::fault_supply,
        {static_cast<std::uint64_t>(request.backing), 0ULL, 0ULL, 0ULL},
        1U);
}

os::core::Result<SyscallRequest> encode_map(
    const os::kernel::MapSyscall& request) noexcept {
    // Four, and there is deliberately no fifth for a length. The mapping covers
    // the grant the backing capability names - see docs/M7_16_MAP.md - so the
    // extent is fixed by the authority rather than restated beside it, and the
    // encoder has nowhere to put a length that could disagree. That is the same
    // shape as fault_supply having nowhere to put a region.
    return encode_call(
        os::kernel::KernelCall::map,
        {static_cast<std::uint64_t>(request.space),
         request.virtual_address,
         static_cast<std::uint64_t>(request.backing),
         static_cast<std::uint64_t>(request.permissions)},
        4U);
}

os::core::Result<SyscallRequest> encode_unmap(
    const os::kernel::UnmapSyscall& request) noexcept {
    // Three, and the third is a capability rather than a length. The extent
    // comes from the grant the backing names, the same way map's does, so there
    // is no length here for a caller to disagree with the authority about.
    return encode_call(
        os::kernel::KernelCall::unmap,
        {static_cast<std::uint64_t>(request.space),
         request.virtual_address,
         static_cast<std::uint64_t>(request.backing),
         0ULL},
        3U);
}

} // namespace os::abi
