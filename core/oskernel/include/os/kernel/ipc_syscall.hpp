#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/capability.hpp>
#include <os/kernel/ipc_endpoint.hpp>

namespace os::kernel {

// Syscall-visible IPC memory is deliberately one fixed-size frame. `send`
// treats it as in/out: request bytes are read before the caller blocks and reply
// bytes overwrite the same frame when the transaction completes. This avoids a
// fourth syscall argument and keeps the kernel ABI surface/argument ceiling
// unchanged.
struct IpcUserExchange final {
    std::uint64_t address {0ULL};
    std::size_t length {0U};

    [[nodiscard]] constexpr bool valid_input() const noexcept {
        return address != 0ULL && length <= max_ipc_inline_bytes;
    }
};

struct IpcSendSyscall final {
    CapabilityId endpoint_capability {invalid_capability};
    IpcUserExchange request {};
};

// A receive that may be bounded in time.
//
// `deadline_nanoseconds` is *relative to the moment of the call*, and zero means
// no deadline - block until a message arrives, which is what receive has always
// done and what every existing caller passes. Relative rather than absolute so
// the caller needs no clock: there is no time syscall to add, no shared time
// page to trust, and two callers cannot agree on a precise absolute instant
// without a channel of their own.
//
// There is deliberately no zero-wait "check without blocking" encoding. Under
// the one-endpoint server model in docs/M7_2_SERVER_LOOP.md a server blocks on
// its endpoint; a cheap non-blocking check exists to drive busy-poll loops, and
// Cookie declines to make that shape convenient. A caller that genuinely wants
// to bound its wait tightly passes a small deadline.
struct IpcReceiveSyscall final {
    CapabilityId endpoint_capability {invalid_capability};
    std::uint64_t exchange_address {0ULL};
    std::uint64_t deadline_nanoseconds {0ULL};

    [[nodiscard]] constexpr bool bounded() const noexcept {
        return deadline_nanoseconds != 0ULL;
    }
};

struct IpcReplySyscall final {
    IpcTransactionId transaction {0U};
    IpcUserExchange response {};
};

namespace ipc_syscall_errors {
inline constexpr std::uint32_t invalid_capability = 240U;
inline constexpr std::uint32_t invalid_exchange = 241U;
inline constexpr std::uint32_t invalid_transaction = 242U;
// A bounded receive was requested and the timer wiring that would honour it is
// not built yet. Deliberately a hard failure rather than a silently ignored
// deadline: a caller that believes it has a bound and does not is worse off
// than one told plainly that it cannot have one.
inline constexpr std::uint32_t deadline_unsupported = 243U;
} // namespace ipc_syscall_errors

// Register contracts. The AArch64 SVC path supplies x0..x2; these decoders are
// architecture-neutral so hostile register values are rejected before any user
// memory ticket is prepared.
[[nodiscard]] os::core::Result<IpcSendSyscall> decode_ipc_send_syscall(
    std::uint64_t x0_capability,
    std::uint64_t x1_exchange_address,
    std::uint64_t x2_request_length) noexcept;

[[nodiscard]] os::core::Result<IpcReceiveSyscall> decode_ipc_receive_syscall(
    std::uint64_t x0_capability,
    std::uint64_t x1_exchange_address,
    std::uint64_t x2_deadline_nanoseconds) noexcept;

[[nodiscard]] os::core::Result<IpcReplySyscall> decode_ipc_reply_syscall(
    std::uint64_t x0_transaction,
    std::uint64_t x1_exchange_address,
    std::uint64_t x2_response_length) noexcept;

} // namespace os::kernel
