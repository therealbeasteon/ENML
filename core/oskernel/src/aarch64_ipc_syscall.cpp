#include <os/kernel/aarch64_ipc_syscall.hpp>

#include <array>
#include <cstddef>
#include <span>

#include <os/core/error.hpp>
#include <os/kernel/aarch64_user_access.hpp>
#include <os/kernel/ipc_syscall.hpp>
#include <os/kernel/user_access.hpp>

#if !defined(__aarch64__)
#error "aarch64_ipc_syscall.cpp must only be compiled for AArch64"
#endif

namespace os::kernel::aarch64 {
namespace {

[[nodiscard]] constexpr os::core::Error machine_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

[[nodiscard]] os::core::Result<ProcessTranslationBinding> current_binding(
    ThreadId current,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    if (current == invalid_thread) return machine_error(ipc_machine_errors::wrong_thread);
    return translations.resolve(current, epochs);
}

[[nodiscard]] os::core::Result<IpcEnvelope> read_envelope(
    ThreadId current,
    AddressSpaceEpoch epoch,
    std::uint64_t address,
    std::size_t length,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    if (length == 0U) return IpcEnvelope{};

    std::array<std::byte, max_ipc_inline_bytes> bytes{};
    auto ticket = prepare_user_access(
        current,
        epoch,
        UserRange{address, length},
        UserAccessIntent::read_from_user,
        translations,
        epochs);
    if (!ticket) return ticket.error();

    auto copied = copy_from_user_current(
        ticket.value(),
        std::span<std::byte>{bytes.data(), length},
        translations,
        epochs);
    if (!copied) return copied.error();
    return IpcEnvelope::from(std::span<const std::byte>{bytes.data(), length});
}

[[nodiscard]] os::core::Result<void> write_envelope(
    ThreadId current,
    AddressSpaceEpoch epoch,
    std::uint64_t address,
    const IpcEnvelope& envelope,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    if (envelope.size == 0U) return {};

    auto ticket = prepare_user_access(
        current,
        epoch,
        UserRange{address, envelope.size},
        UserAccessIntent::write_to_user,
        translations,
        epochs);
    if (!ticket) return ticket.error();
    return copy_to_user_current(
        ticket.value(), envelope.view(), translations, epochs);
}

} // namespace

os::core::Result<IpcSvcResult> dispatch_ipc_svc_current(
    ThreadId current,
    KernelCall call,
    ExceptionFrame& frame,
    Kernel& kernel,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    auto binding = current_binding(current, translations, epochs);
    if (!binding) return binding.error();

    switch (call) {
    case KernelCall::send: {
        auto decoded = decode_ipc_send_syscall(frame.x[0], frame.x[1], frame.x[2]);
        if (!decoded) return decoded.error();
        auto envelope = read_envelope(
            current,
            binding.value().epoch,
            decoded.value().request.address,
            decoded.value().request.length,
            translations,
            epochs);
        if (!envelope) return envelope.error();

        auto armed = kernel.ipc_arm_send_continuation(
            current,
            binding.value().epoch,
            decoded.value().request.address,
            epochs);
        if (!armed) return armed.error();

        auto sent = kernel.ipc_send(
            current,
            decoded.value().endpoint_capability,
            envelope.value());
        if (!sent) {
            (void)kernel.ipc_cancel_send_continuation(current);
            return sent.error();
        }
        // send is synchronous: success here means the request is in flight and
        // this thread is no longer runnable until a reply or endpoint death.
        return IpcSvcResult{.reschedule = true, .completed = false};
    }

    case KernelCall::receive: {
        auto decoded = decode_ipc_receive_syscall(frame.x[0], frame.x[1]);
        if (!decoded) return decoded.error();
        auto received = kernel.ipc_receive(current, decoded.value().endpoint_capability);
        if (!received) return received.error();
        if (!received.value().valid()) {
            // Receiver-first rendezvous is represented by a blocked thread.
            // A receive-continuation will carry payload completion in the next
            // slice; sender-first native IPC (the first QEMU proof) completes
            // immediately here.
            return IpcSvcResult{.reschedule = true, .completed = false};
        }
        auto wrote = write_envelope(
            current,
            binding.value().epoch,
            decoded.value().exchange_address,
            received.value().request,
            translations,
            epochs);
        if (!wrote) return wrote.error();
        frame.x[0] = received.value().reply.transaction;
        frame.x[1] = received.value().request.size;
        return IpcSvcResult{.reschedule = false, .completed = true};
    }

    case KernelCall::reply: {
        auto decoded = decode_ipc_reply_syscall(frame.x[0], frame.x[1], frame.x[2]);
        if (!decoded) return decoded.error();
        auto response = read_envelope(
            current,
            binding.value().epoch,
            decoded.value().response.address,
            decoded.value().response.length,
            translations,
            epochs);
        if (!response) return response.error();
        auto replied = kernel.ipc_reply_transaction(
            current,
            decoded.value().transaction,
            response.value());
        if (!replied) return replied.error();
        frame.x[0] = 0U;
        return IpcSvcResult{.reschedule = true, .completed = true};
    }

    default:
        return machine_error(ipc_machine_errors::bad_syscall);
    }
}

os::core::Result<bool> complete_ipc_send_current(
    ThreadId current,
    ExceptionFrame& frame,
    Kernel& kernel,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    if (!kernel.ipc().reply_available(current)) return false;

    auto binding = current_binding(current, translations, epochs);
    if (!binding) return binding.error();

    auto continuation = kernel.ipc_take_send_continuation(
        current, binding.value().epoch, epochs);
    if (!continuation) {
        // Do not leave a completed reply occupying bounded kernel state when its
        // original memory incarnation has disappeared.
        (void)kernel.ipc_take_reply(current);
        return continuation.error();
    }

    auto response = kernel.ipc_take_reply(current);
    if (!response) return response.error();
    auto wrote = write_envelope(
        current,
        continuation.value().epoch,
        continuation.value().exchange_address,
        response.value(),
        translations,
        epochs);
    if (!wrote) return wrote.error();

    frame.x[0] = response.value().size;
    frame.x[1] = 0U;
    return true;
}

} // namespace os::kernel::aarch64
