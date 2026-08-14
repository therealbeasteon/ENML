#include <os/kernel/ipc_syscall.hpp>

#include <limits>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {
[[nodiscard]] constexpr os::core::Error ipc_syscall_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

[[nodiscard]] constexpr bool fits_size_t(std::uint64_t value) noexcept {
    if constexpr (sizeof(std::size_t) >= sizeof(std::uint64_t)) return true;
    return value <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
}
}

os::core::Result<IpcSendSyscall> decode_ipc_send_syscall(
    std::uint64_t x0_capability,
    std::uint64_t x1_exchange_address,
    std::uint64_t x2_request_length) noexcept {
    if (x0_capability == 0ULL) {
        return ipc_syscall_error(ipc_syscall_errors::invalid_capability);
    }
    if (x1_exchange_address == 0ULL || !fits_size_t(x2_request_length) ||
        x2_request_length > max_ipc_inline_bytes) {
        return ipc_syscall_error(ipc_syscall_errors::invalid_exchange);
    }
    return IpcSendSyscall{
        .endpoint_capability = static_cast<CapabilityId>(x0_capability),
        .request = IpcUserExchange{
            .address = x1_exchange_address,
            .length = static_cast<std::size_t>(x2_request_length),
        },
    };
}

os::core::Result<IpcReceiveSyscall> decode_ipc_receive_syscall(
    std::uint64_t x0_capability,
    std::uint64_t x1_exchange_address,
    std::uint64_t x2_deadline_nanoseconds) noexcept {
    if (x0_capability == 0ULL) {
        return ipc_syscall_error(ipc_syscall_errors::invalid_capability);
    }
    if (x1_exchange_address == 0ULL) {
        return ipc_syscall_error(ipc_syscall_errors::invalid_exchange);
    }
    // Every register value is a legal deadline: zero is no deadline and any
    // other value is a relative nanosecond count the scheduler will clamp
    // against its own horizon. Nothing to reject here, which is the point of
    // relative-and-unsigned - there is no negative timeout, no absolute value
    // already in the past, and no encoding a hostile caller can use to mean
    // something the decoder did not intend.
    return IpcReceiveSyscall{
        .endpoint_capability = static_cast<CapabilityId>(x0_capability),
        .exchange_address = x1_exchange_address,
        .deadline_nanoseconds = x2_deadline_nanoseconds,
    };
}

os::core::Result<IpcReplySyscall> decode_ipc_reply_syscall(
    std::uint64_t x0_transaction,
    std::uint64_t x1_exchange_address,
    std::uint64_t x2_response_length) noexcept {
    if (x0_transaction == 0ULL) {
        return ipc_syscall_error(ipc_syscall_errors::invalid_transaction);
    }
    if (x1_exchange_address == 0ULL || !fits_size_t(x2_response_length) ||
        x2_response_length > max_ipc_inline_bytes) {
        return ipc_syscall_error(ipc_syscall_errors::invalid_exchange);
    }
    return IpcReplySyscall{
        .transaction = x0_transaction,
        .response = IpcUserExchange{
            .address = x1_exchange_address,
            .length = static_cast<std::size_t>(x2_response_length),
        },
    };
}

} // namespace os::kernel
