#include <os/kernel/thread_admission.hpp>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error admission_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

} // namespace

os::core::Result<ThreadCreateSyscall> decode_thread_create_syscall(
    std::uint64_t x0_space,
    std::uint64_t x1_stack) noexcept {
    // Zero is the only value this layer can reject for either argument.
    // Whether the capability names an address space, whether this caller holds
    // it with the right to admit, and whether the stack is mapped are checked
    // where they are enforced - a second weaker copy of any of those beside the
    // enforcing one is how the two come to disagree.
    if (x0_space == 0ULL) {
        return os::core::Result<ThreadCreateSyscall>{
            admission_error(thread_admission_errors::invalid_capability)};
    }
    if (x1_stack == 0ULL) {
        return os::core::Result<ThreadCreateSyscall>{
            admission_error(thread_admission_errors::invalid_stack)};
    }
    return ThreadCreateSyscall{static_cast<CapabilityId>(x0_space), x1_stack};
}

os::core::Result<ThreadId> ThreadIdentifierIssuer::issue() noexcept {
    // Refuses at the top of the range rather than wrapping. There is no
    // recycling behind this: an identifier is spent when it is issued, whether
    // or not the thread it named still exists.
    if (next_ == 0xFFFF'FFFFU) {
        return os::core::Result<ThreadId>{
            admission_error(thread_admission_errors::identifier_exhausted)};
    }
    const ThreadId issued = next_;
    ++next_;
    return issued;
}

} // namespace os::kernel
