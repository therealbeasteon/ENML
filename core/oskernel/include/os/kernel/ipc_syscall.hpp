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

struct IpcReceiveSyscall final {
    CapabilityId endpoint_capability {invalid_capability};
    std::uint64_t exchange_address {0ULL};
};

struct IpcReplySyscall final {
    IpcTransactionId transaction {0U};
    IpcUserExchange response {};
};

namespace ipc_syscall_errors {
inline constexpr std::uint32_t invalid_capability = 240U;
inline constexpr std::uint32_t invalid_exchange = 241U;
inline constexpr std::uint32_t invalid_transaction = 242U;
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
    std::uint64_t x1_exchange_address) noexcept;

[[nodiscard]] os::core::Result<IpcReplySyscall> decode_ipc_reply_syscall(
    std::uint64_t x0_transaction,
    std::uint64_t x1_exchange_address,
    std::uint64_t x2_response_length) noexcept;

} // namespace os::kernel
