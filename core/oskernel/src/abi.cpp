#include <os/kernel/abi.hpp>

#include <array>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error kernel_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

// The surface, in one place, in call order.
//
// Written as data rather than as a switch so that the whole interface can be
// counted, enumerated and asserted against the ceiling. A switch would let the
// surface grow without anything noticing.
inline constexpr std::array<CallDescriptor, 16U> call_table{
    // The unprivileged core. send and receive block by definition - a
    // synchronous message pass is a rendezvous - and reply does not, because a
    // server that could be blocked by a client refusing to collect its answer
    // would be a denial of service with no defence.
    CallDescriptor{KernelCall::send, CallAuthority::unprivileged, 3U, true},
    CallDescriptor{KernelCall::receive, CallAuthority::unprivileged, 2U, true},
    CallDescriptor{KernelCall::reply, CallAuthority::unprivileged, 3U, false},
    CallDescriptor{KernelCall::yield, CallAuthority::unprivileged, 0U, false},
    CallDescriptor{KernelCall::thread_exit, CallAuthority::unprivileged, 1U, false},

    CallDescriptor{KernelCall::thread_create, CallAuthority::process_control, 4U, false},

    CallDescriptor{KernelCall::address_space_create, CallAuthority::process_control, 2U, false},
    CallDescriptor{KernelCall::address_space_destroy, CallAuthority::process_control, 1U, false},

    CallDescriptor{KernelCall::map, CallAuthority::memory_control, 4U, false},
    CallDescriptor{KernelCall::unmap, CallAuthority::memory_control, 3U, false},

    // interrupt_complete does not block: a driver telling the kernel its device
    // is quiet must never be made to wait, or an interrupt storm becomes a
    // livelock in the driver rather than a handled condition.
    CallDescriptor{KernelCall::interrupt_attach, CallAuthority::interrupt_control, 1U, false},
    CallDescriptor{KernelCall::interrupt_detach, CallAuthority::interrupt_control, 1U, false},
    CallDescriptor{KernelCall::interrupt_complete, CallAuthority::interrupt_control, 1U, false},

    CallDescriptor{KernelCall::capability_grant, CallAuthority::capability_control, 4U, false},
    CallDescriptor{KernelCall::capability_revoke, CallAuthority::capability_control, 2U, false},
    // memory_control rather than a class of its own: supplying backing for a
    // fault is establishing a mapping on someone else's behalf, which is the
    // authority that class already describes.
    CallDescriptor{KernelCall::fault_supply, CallAuthority::memory_control, 1U, false},
};

static_assert(
    call_table.size() <= max_kernel_calls,
    "the kernel system call surface has exceeded its ceiling; remove a call or "
    "move it into a service rather than raising the limit");

} // namespace

std::size_t kernel_call_count() noexcept {
    return call_table.size();
}

os::core::Result<CallDescriptor> call_at(std::size_t index) noexcept {
    if (index >= call_table.size()) {
        return os::core::Result<CallDescriptor>{kernel_error(errors::unknown_call)};
    }
    return os::core::Result<CallDescriptor>{call_table[index]};
}

os::core::Result<CallDescriptor> describe_call(KernelCall call) noexcept {
    for (const auto& descriptor : call_table) {
        if (descriptor.call == call) {
            return os::core::Result<CallDescriptor>{descriptor};
        }
    }
    return os::core::Result<CallDescriptor>{kernel_error(errors::unknown_call)};
}

os::core::Result<CallDescriptor> decode_call(std::uint16_t raw) noexcept {
    // Searched rather than indexed. The table happens to be dense and ordered
    // today, and turning that into an array subscript would make the kernel
    // entry path depend on a property no one is enforcing - the first
    // reordering or removal would turn an unknown call number into an
    // out-of-bounds read on the most attacker-reachable path in the system.
    for (const auto& descriptor : call_table) {
        if (static_cast<std::uint16_t>(descriptor.call) == raw) {
            return os::core::Result<CallDescriptor>{descriptor};
        }
    }
    return os::core::Result<CallDescriptor>{kernel_error(errors::unknown_call)};
}

} // namespace os::kernel
