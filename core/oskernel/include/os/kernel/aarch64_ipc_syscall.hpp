#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/abi.hpp>
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

struct IpcSvcResult final {
    bool reschedule {false};
    bool completed {false};
};

[[nodiscard]] os::core::Result<IpcSvcResult> dispatch_ipc_svc_current(
    ThreadId current,
    KernelCall call,
    ExceptionFrame& frame,
    Kernel& kernel,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept;

[[nodiscard]] os::core::Result<bool> complete_ipc_send_current(
    ThreadId current,
    ExceptionFrame& frame,
    Kernel& kernel,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept;

} // namespace os::kernel::aarch64
