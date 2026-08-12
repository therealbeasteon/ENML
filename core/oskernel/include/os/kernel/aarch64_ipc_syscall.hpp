#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/aarch64_exception.hpp>
#include <os/kernel/address_space_epoch.hpp>
#include <os/kernel/kernel.hpp>
#include <os/kernel/process_translation.hpp>

namespace os::kernel::aarch64 {

namespace ipc_machine_errors {
inline constexpr std::uint32_t wrong_thread = 270U;
inline constexpr std::uint32_t bad_syscall = 271U;
inline constexpr std::uint32_t copy_failed = 272U;
inline constexpr std::uint32_t no_reply_completion = 273U;
} // namespace ipc_machine_errors

// Result of one IPC SVC operation before scheduling is reconsidered. A blocking
// send/receive changes Kernel runnability; the boot/production entry path then
// asks PreemptionCoordinator to reschedule and commits the returned execution
// universe. Keeping that commit outside this object prevents IPC from owning
// machine scheduling policy.
struct IpcSvcResult final {
    bool reschedule {false};
    bool completed {false};
};

// Executes only Cookie's existing send/receive/reply ABI against the currently
// installed process translation. User pointers are converted to single-page
// tickets and copied with the guarded AArch64 user-access machinery.
[[nodiscard]] os::core::Result<IpcSvcResult> dispatch_ipc_svc_current(
    ThreadId current,
    KernelCall call,
    ExceptionFrame& frame,
    Kernel& kernel,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept;

// Called after a scheduler transition has installed `current`'s translation but
// before ERET. If `current` is returning from a blocked send, consume exactly
// one completed reply and its epoch-bound continuation, copy the response into
// the original exchange frame, and set x0 to the reply length. If no completion
// exists this is a no-op success.
[[nodiscard]] os::core::Result<bool> complete_ipc_send_current(
    ThreadId current,
    ExceptionFrame& frame,
    Kernel& kernel,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept;

} // namespace os::kernel::aarch64
