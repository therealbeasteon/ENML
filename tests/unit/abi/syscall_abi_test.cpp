#include <cstdint>
#include <cstdio>

#include <os/abi/syscall.hpp>
#include <os/core/error.hpp>
#include <os/kernel/abi.hpp>
#include <os/kernel/address_space_syscall.hpp>
#include <os/kernel/fault_delivery.hpp>
#include <os/kernel/ipc_syscall.hpp>
#include <os/kernel/map_syscall.hpp>
#include <os/kernel/thread_admission.hpp>

// The user half of the syscall ABI, checked against the kernel's own readers.
//
// The differential cases below are the point of the file. An encoder tested
// against a second reading of the ABI document proves the author read it twice;
// an encoder tested against the decoder that will actually consume its output
// proves the two cannot disagree. docs/M7_12_CKX_FORMAT.md's writer established
// that rule for the image format and it applies unchanged at the register
// boundary.

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "syscall abi: %s\n", what);
    }
    return condition;
}

// Distinctive values rather than 1, 2, 3. A transposition of two adjacent
// registers is the defect this whole file exists to catch, and small ordinals
// make one look like the other.
constexpr std::uint64_t marker_capability = 0x5EA1'0000'0000'00A1ULL;
constexpr std::uint64_t marker_address = 0x0000'BEEF'0000'1000ULL;
constexpr std::uint64_t marker_length = 0x0000'0000'0000'0040ULL;
constexpr std::uint64_t marker_deadline = 0x0000'0000'0BAD'1DEAULL;
constexpr std::uint64_t marker_stack = 0x0000'7FFF'FFFF'0000ULL;
// Distinct from marker_capability, because map carries two capabilities in two
// non-adjacent registers and one value for both would let a swap pass.
constexpr std::uint64_t marker_backing = 0x5EA1'0000'0000'00B2ULL;

// Every register past a call's declared argument count must be zero. Checked
// for every call rather than asserted once, because the property is a promise
// about each encoder and not about encode_call alone.
bool trailing_registers_zero(
    const os::abi::SyscallRequest& request, std::size_t declared) {
    for (std::size_t index = declared; index < os::abi::argument_registers; ++index) {
        if (request.arguments[index] != 0ULL) return false;
    }
    return true;
}

} // namespace

int main() {
    // --- The surface is partitioned: every call is implemented or listed as
    // not, never both and never neither.
    //
    // This is what makes a seventeenth call fail here rather than at EL0. The
    // ABI table enumerates itself, so nothing has to be kept in step by hand.
    for (std::size_t index = 0U; index < os::kernel::kernel_call_count(); ++index) {
        auto entry = os::kernel::call_at(index);
        if (!check(static_cast<bool>(entry), "ABI table entry unreachable")) return 1;
        const auto call = entry.value().call;

        bool listed_absent = false;
        for (const auto absent : os::abi::calls_without_stubs) {
            if (absent == call) listed_absent = true;
        }
        const bool implemented = os::abi::call_has_stub(call);

        if (!check(implemented != listed_absent,
                   "a call is either encodable or listed as not; this one is "
                   "both or neither")) {
            std::fprintf(
                stderr, "  call number %u\n", static_cast<unsigned>(call));
            return 1;
        }
    }

    // --- send: three registers, round-tripped through the kernel's decoder.
    {
        const os::kernel::IpcSendSyscall original{
            static_cast<os::kernel::CapabilityId>(marker_capability),
            os::kernel::IpcUserExchange{marker_address, static_cast<std::size_t>(marker_length)}};
        auto encoded = os::abi::encode_send(original);
        if (!check(static_cast<bool>(encoded), "send refused encoding")) return 1;
        if (!check(encoded.value().call ==
                       static_cast<std::uint16_t>(os::kernel::KernelCall::send),
                   "send encoded the wrong call number")) return 1;
        if (!check(trailing_registers_zero(encoded.value(), 3U),
                   "send left a register past its argument count non-zero")) return 1;

        auto decoded = os::kernel::decode_ipc_send_syscall(
            encoded.value().arguments[0],
            encoded.value().arguments[1],
            encoded.value().arguments[2]);
        if (!check(static_cast<bool>(decoded),
                   "the kernel rejected what the stub encoded for send")) return 1;
        if (!check(decoded.value().endpoint_capability == original.endpoint_capability &&
                       decoded.value().request.address == original.request.address &&
                       decoded.value().request.length == original.request.length,
                   "send did not survive encode/decode field-for-field")) return 1;
    }

    // --- receive: three registers, and the deadline is the reason this case
    // matters most.
    //
    // The ABI table declared two arguments for receive from the day it was
    // written until this milestone, while the decoder took three. An encoder
    // trusting the old count would write zero into x2, and `bounded()` would be
    // false for every caller that asked for a timeout - a caller believing it
    // has a bound and not having one. The non-zero deadline below is what makes
    // that failure visible rather than plausible.
    {
        const os::kernel::IpcReceiveSyscall original{
            static_cast<os::kernel::CapabilityId>(marker_capability),
            marker_address,
            marker_deadline};
        auto encoded = os::abi::encode_receive(original);
        if (!check(static_cast<bool>(encoded), "receive refused encoding")) return 1;
        if (!check(trailing_registers_zero(encoded.value(), 3U),
                   "receive left a register past its argument count non-zero")) return 1;

        auto decoded = os::kernel::decode_ipc_receive_syscall(
            encoded.value().arguments[0],
            encoded.value().arguments[1],
            encoded.value().arguments[2]);
        if (!check(static_cast<bool>(decoded),
                   "the kernel rejected what the stub encoded for receive")) return 1;
        if (!check(decoded.value().endpoint_capability == original.endpoint_capability &&
                       decoded.value().exchange_address == original.exchange_address &&
                       decoded.value().deadline_nanoseconds == original.deadline_nanoseconds,
                   "receive did not survive encode/decode field-for-field")) return 1;
        if (!check(decoded.value().bounded(),
                   "a receive encoded with a deadline arrived unbounded")) return 1;
    }

    // --- reply.
    {
        const os::kernel::IpcReplySyscall original{
            static_cast<os::kernel::IpcTransactionId>(0x0000'0000'0000'2BADULL),
            os::kernel::IpcUserExchange{marker_address, static_cast<std::size_t>(marker_length)}};
        auto encoded = os::abi::encode_reply(original);
        if (!check(static_cast<bool>(encoded), "reply refused encoding")) return 1;
        if (!check(trailing_registers_zero(encoded.value(), 3U),
                   "reply left a register past its argument count non-zero")) return 1;

        auto decoded = os::kernel::decode_ipc_reply_syscall(
            encoded.value().arguments[0],
            encoded.value().arguments[1],
            encoded.value().arguments[2]);
        if (!check(static_cast<bool>(decoded),
                   "the kernel rejected what the stub encoded for reply")) return 1;
        if (!check(decoded.value().transaction == original.transaction &&
                       decoded.value().response.address == original.response.address &&
                       decoded.value().response.length == original.response.length,
                   "reply did not survive encode/decode field-for-field")) return 1;
    }

    // --- thread_create. Two registers, and the assertion worth writing is
    // about the two it does *not* carry: no entry point and no thread
    // identifier reach the kernel from here. A stub is exactly where those
    // would reappear as a convenience - docs/M7_12_ENTRY_BINDING.md.
    {
        const os::kernel::ThreadCreateSyscall original{
            static_cast<os::kernel::CapabilityId>(marker_capability), marker_stack};
        auto encoded = os::abi::encode_thread_create(original);
        if (!check(static_cast<bool>(encoded), "thread_create refused encoding")) return 1;
        if (!check(trailing_registers_zero(encoded.value(), 2U),
                   "thread_create carried a third register the ABI does not "
                   "define - an entry or an identifier would arrive exactly "
                   "here")) return 1;

        auto decoded = os::kernel::decode_thread_create_syscall(
            encoded.value().arguments[0], encoded.value().arguments[1]);
        if (!check(static_cast<bool>(decoded),
                   "the kernel rejected what the stub encoded for thread_create")) return 1;
        if (!check(decoded.value().space == original.space &&
                       decoded.value().stack == original.stack,
                   "thread_create did not survive encode/decode field-for-field")) return 1;
    }

    // --- address_space_create and destroy.
    {
        const os::kernel::AddressSpaceCreateSyscall original{
            static_cast<os::kernel::CapabilityId>(marker_capability),
            static_cast<os::kernel::CapabilityId>(marker_address)};
        auto encoded = os::abi::encode_address_space_create(original);
        if (!check(static_cast<bool>(encoded),
                   "address_space_create refused encoding")) return 1;
        if (!check(trailing_registers_zero(encoded.value(), 2U),
                   "address_space_create left a trailing register non-zero")) return 1;

        auto decoded = os::kernel::decode_address_space_create_syscall(
            encoded.value().arguments[0], encoded.value().arguments[1]);
        if (!check(static_cast<bool>(decoded),
                   "the kernel rejected what the stub encoded for "
                   "address_space_create")) return 1;
        if (!check(decoded.value().authority == original.authority &&
                       decoded.value().root_grant == original.root_grant,
                   "address_space_create did not survive encode/decode")) return 1;
    }
    {
        const os::kernel::AddressSpaceDestroySyscall original{
            static_cast<os::kernel::CapabilityId>(marker_capability)};
        auto encoded = os::abi::encode_address_space_destroy(original);
        if (!check(static_cast<bool>(encoded),
                   "address_space_destroy refused encoding")) return 1;
        if (!check(trailing_registers_zero(encoded.value(), 1U),
                   "address_space_destroy left a trailing register non-zero")) return 1;

        auto decoded = os::kernel::decode_address_space_destroy_syscall(
            encoded.value().arguments[0]);
        if (!check(static_cast<bool>(decoded),
                   "the kernel rejected what the stub encoded for "
                   "address_space_destroy")) return 1;
        if (!check(decoded.value().space == original.space,
                   "address_space_destroy did not survive encode/decode")) return 1;
    }

    // --- fault_supply. One register, and the trailing-zero assertion is the
    // interesting half: a pager has nowhere to put a region identifier, which
    // is the stronger form of "a pager must not answer a question it was never
    // asked".
    {
        const os::kernel::FaultSupplySyscall original{
            static_cast<os::kernel::CapabilityId>(marker_capability)};
        auto encoded = os::abi::encode_fault_supply(original);
        if (!check(static_cast<bool>(encoded), "fault_supply refused encoding")) return 1;
        if (!check(trailing_registers_zero(encoded.value(), 1U),
                   "fault_supply carried a register beyond the backing - a "
                   "region identifier would arrive exactly here")) return 1;

        auto decoded = os::kernel::decode_fault_supply_syscall(
            encoded.value().arguments[0]);
        if (!check(static_cast<bool>(decoded),
                   "the kernel rejected what the stub encoded for fault_supply")) return 1;
        if (!check(decoded.value().backing == original.backing,
                   "fault_supply did not survive encode/decode")) return 1;
    }

    // --- map. Four registers, and the trailing-zero assertion is the half that
    // matters here for the same reason it did for fault_supply: a caller has
    // nowhere to put a *length*, which is the stronger form of
    // docs/M7_16_MAP.md's decision that the extent comes from the grant. A fifth
    // register would arrive exactly here.
    {
        const os::kernel::MapSyscall original{
            static_cast<os::kernel::CapabilityId>(marker_capability),
            marker_address,
            static_cast<os::kernel::CapabilityId>(marker_backing),
            os::kernel::MapPermissions::read_execute,
        };
        auto encoded = os::abi::encode_map(original);
        if (!check(static_cast<bool>(encoded), "map refused encoding")) return 1;
        if (!check(trailing_registers_zero(encoded.value(), 4U),
                   "map carried a register beyond its four - a length would "
                   "arrive exactly here")) return 1;

        auto decoded = os::kernel::decode_map_syscall(
            encoded.value().arguments[0],
            encoded.value().arguments[1],
            encoded.value().arguments[2],
            encoded.value().arguments[3]);
        if (!check(static_cast<bool>(decoded),
                   "the kernel rejected what the stub encoded for map")) return 1;
        // Each field separately. The two capabilities are in x0 and x2 with an
        // address between them, which is exactly the shape a transposition
        // survives if only one field is compared.
        if (!check(decoded.value().space == original.space,
                   "map's space did not survive encode/decode")) return 1;
        if (!check(decoded.value().virtual_address == original.virtual_address,
                   "map's virtual address did not survive encode/decode")) return 1;
        if (!check(decoded.value().backing == original.backing,
                   "map's backing did not survive encode/decode")) return 1;
        if (!check(decoded.value().permissions == original.permissions,
                   "map's permissions did not survive encode/decode")) return 1;
    }

    // --- yield takes nothing, and still writes the whole register set.
    // "Takes nothing" and "leaves whatever was there" are different statements.
    {
        auto encoded = os::abi::encode_yield();
        if (!check(static_cast<bool>(encoded), "yield refused encoding")) return 1;
        if (!check(trailing_registers_zero(encoded.value(), 0U),
                   "yield left a register non-zero")) return 1;
    }

    // --- encode_call refuses rather than pads.
    {
        auto wrong_count = os::abi::encode_call(
            os::kernel::KernelCall::send, {1ULL, 2ULL, 3ULL, 4ULL}, 4U);
        if (!check(!wrong_count, "an argument count the table does not declare "
                                 "was accepted")) return 1;
        if (!check(wrong_count.error().code == os::abi::abi_errors::argument_count,
                   "wrong argument count reported as something else")) return 1;

        auto unknown = os::abi::encode_call(
            static_cast<os::kernel::KernelCall>(0U), {0ULL, 0ULL, 0ULL, 0ULL}, 0U);
        if (!check(!unknown, "call number zero was accepted")) return 1;
        if (!check(unknown.error().code == os::abi::abi_errors::unknown_call,
                   "unknown call reported as something else")) return 1;
    }

    // --- The outcome convention.
    {
        const std::array<std::uint64_t, os::abi::argument_registers> results{
            marker_capability, marker_address, marker_length, marker_deadline};

        auto answered = os::abi::decode_outcome(os::abi::outcome_answered, results);
        if (!check(static_cast<bool>(answered), "an answered outcome was refused")) return 1;
        if (!check(answered.value().results == results,
                   "an answered outcome altered its results")) return 1;

        // A refusal carries a whole Error across, domain included. POSIX loses
        // the domain here and keeps only a code.
        const os::core::Error refusal{os::core::ErrorDomain::kernel, 0U, 4242U};
        const std::array<std::uint64_t, os::abi::argument_registers> refused_registers{
            os::abi::encode_error(refusal), 0ULL, 0ULL, 0ULL};
        auto refused = os::abi::decode_outcome(os::abi::outcome_refused, refused_registers);
        if (!check(!refused, "a refusal decoded as an answer")) return 1;
        if (!check(refused.error() == refusal,
                   "a refusal did not carry its Error across intact")) return 1;

        // The case the whole design exists for. The caller zeroes x7 before the
        // trap, so a kernel path that returns without writing an outcome leaves
        // this - and it must not read as success.
        auto silent = os::abi::decode_outcome(0ULL, results);
        if (!check(!silent, "a zeroed outcome register read as an answer")) return 1;
        if (!check(silent.error().code == os::abi::abi_errors::no_answer,
                   "a zeroed outcome was reported as something other than "
                   "'no answer'")) return 1;

        // And nothing else is an outcome either - not all-ones, not a value one
        // bit away from a tag.
        for (const std::uint64_t junk :
             {~0ULL, 1ULL, os::abi::outcome_answered ^ 1ULL,
              os::abi::outcome_refused ^ (1ULL << 63U)}) {
            auto rejected = os::abi::decode_outcome(junk, results);
            if (!check(!rejected, "a value that is not a tag read as an outcome")) return 1;
            if (!check(rejected.error().code == os::abi::abi_errors::no_answer,
                       "a non-tag outcome was misreported")) return 1;
        }
    }

    // --- Error encoding is exact, and reserved must stay empty.
    {
        for (const os::core::Error original :
             {os::core::Error{os::core::ErrorDomain::kernel, 0U, 0U},
              os::core::Error{os::core::ErrorDomain::image, 0U, 0xFFFF'FFFFU},
              os::core::Error{os::core::ErrorDomain::core, 0U, 1U}}) {
            auto round_tripped = os::abi::decode_error(os::abi::encode_error(original));
            if (!check(static_cast<bool>(round_tripped),
                       "a well-formed Error was refused")) return 1;
            if (!check(round_tripped.value().reason == original,
                       "an Error did not survive the register boundary")) return 1;
        }

        // A field with no meaning must carry no content, or it is a channel.
        auto polluted = os::abi::decode_error(0x000B'0001'0000'0001ULL);
        if (!check(!polluted, "a non-zero reserved field was accepted")) return 1;
        if (!check(polluted.error().code == os::abi::abi_errors::malformed_error,
                   "a polluted reserved field was misreported")) return 1;
    }

    std::fprintf(
        stderr,
        "syscall abi: %zu calls, %zu encodable, %zu listed as not yet callable\n",
        os::kernel::kernel_call_count(),
        os::kernel::kernel_call_count() - os::abi::calls_without_stubs.size(),
        os::abi::calls_without_stubs.size());
    return 0;
}
