#include <os/kernel/aarch64_ipc_syscall.hpp>

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

[[nodiscard]] os::core::Result<UserAccessTicket> ticket_from_binding(
    const ProcessTranslationBinding& binding,
    UserRange range,
    UserAccessIntent intent) noexcept {
    if (!binding.valid()) return machine_error(user_access_errors::stale_translation);
    if (!range.valid()) return machine_error(user_access_errors::invalid_range);
    return UserAccessTicket{
        .thread = binding.thread,
        .epoch = binding.epoch,
        .root_physical = binding.root_physical,
        .range = range,
        .intent = intent,
    };
}

[[nodiscard]] os::core::Result<IpcEnvelope> read_envelope(
    const ProcessTranslationBinding& binding,
    std::uint64_t address,
    std::size_t length,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    if (length == 0U) return IpcEnvelope{};
    if (length > max_ipc_inline_bytes) return machine_error(ipc_errors::payload_too_large);

    auto ticket = ticket_from_binding(
        binding,
        UserRange{address, length},
        UserAccessIntent::read_from_user);
    if (!ticket) return ticket.error();

    IpcEnvelope envelope{};
    auto copied = copy_from_user_current(
        ticket.value(),
        std::span<std::byte>{envelope.bytes.data(), length},
        translations,
        epochs);
    if (!copied) return copied.error();
    envelope.size = static_cast<std::uint8_t>(length);
    return envelope;
}

[[nodiscard]] os::core::Result<void> write_envelope(
    const ProcessTranslationBinding& binding,
    std::uint64_t address,
    const IpcEnvelope& envelope,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    if (envelope.size == 0U) return {};

    auto ticket = ticket_from_binding(
        binding,
        UserRange{address, envelope.size},
        UserAccessIntent::write_to_user);
    if (!ticket) return ticket.error();
    return copy_to_user_current(ticket.value(), envelope.view(), translations, epochs);
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
            binding.value(),
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
        return IpcSvcResult{.reschedule = true, .completed = false};
    }

    case KernelCall::receive: {
        auto decoded = decode_ipc_receive_syscall(frame.x[0], frame.x[1]);
        if (!decoded) return decoded.error();

        // Arm before rendezvous state can block. Immediate delivery cancels the
        // continuation again; failure never leaves a sleeping syscall without
        // the state required to complete it later.
        auto armed = kernel.ipc_arm_receive_continuation(
            current,
            binding.value().epoch,
            decoded.value().endpoint_capability,
            decoded.value().exchange_address,
            epochs);
        if (!armed) return armed.error();

        auto received = kernel.ipc_receive(current, decoded.value().endpoint_capability);
        if (!received) {
            (void)kernel.ipc_cancel_receive_continuation(current);
            return received.error();
        }
        if (!received.value().valid()) {
            return IpcSvcResult{.reschedule = true, .completed = false};
        }

        (void)kernel.ipc_cancel_receive_continuation(current);
        auto wrote = write_envelope(
            binding.value(),
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
            binding.value(),
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

os::core::Result<bool> complete_ipc_current(
    ThreadId current,
    ExceptionFrame& frame,
    Kernel& kernel,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    auto binding = current_binding(current, translations, epochs);
    if (!binding) return binding.error();

    if (kernel.ipc_continuations().receive_armed(current)) {
        auto continuation = kernel.ipc_take_receive_continuation(
            current, binding.value().epoch, epochs);
        if (!continuation) return continuation.error();

        auto received = kernel.ipc_receive(
            current, continuation.value().endpoint_capability);
        if (!received || !received.value().valid()) {
            return machine_error(ipc_machine_errors::no_receive_completion);
        }
        auto wrote = write_envelope(
            binding.value(),
            continuation.value().exchange_address,
            received.value().request,
            translations,
            epochs);
        if (!wrote) return wrote.error();
        frame.x[0] = received.value().reply.transaction;
        frame.x[1] = received.value().request.size;
        return true;
    }

    if (!kernel.ipc().reply_available(current)) return false;

    auto continuation = kernel.ipc_take_send_continuation(
        current, binding.value().epoch, epochs);
    if (!continuation) {
        (void)kernel.ipc_take_reply(current);
        return continuation.error();
    }

    auto response = kernel.ipc_take_reply(current);
    if (!response) return response.error();
    auto wrote = write_envelope(
        binding.value(),
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
