#include <os/kernel/executable_region.hpp>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {
[[nodiscard]] constexpr os::core::Error executable_region_error(
    std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}
} // namespace

bool ExecutableRegionTable::would_refuse(AddressSpaceIdentity space) const noexcept {
    if (!space.valid()) return true;
    for (const auto& region : regions_) {
        if (region.valid() && region.space == space) return true;
    }
    return false;
}

os::core::Result<void> ExecutableRegionTable::record(
    AddressSpaceIdentity space,
    std::uint64_t base,
    std::uint64_t length) noexcept {
    if (!space.valid() || length == 0ULL) {
        return executable_region_error(executable_region_errors::invalid_space);
    }
    // Re-checked here rather than trusting the caller's earlier would_refuse.
    // The two calls straddle the machine layer, and a check whose result is
    // carried across an operation is a check that was true once.
    for (const auto& region : regions_) {
        if (region.valid() && region.space == space) {
            return executable_region_error(
                executable_region_errors::already_executable);
        }
    }
    for (auto& region : regions_) {
        if (!region.valid()) {
            region = ExecutableRegion{space, base, length};
            ++live_;
            return {};
        }
    }
    return executable_region_error(executable_region_errors::exhausted);
}

os::core::Result<ExecutableRegion> ExecutableRegionTable::region_for(
    AddressSpaceIdentity space) const noexcept {
    if (!space.valid()) {
        return os::core::Result<ExecutableRegion>{
            executable_region_error(executable_region_errors::invalid_space)};
    }
    for (const auto& region : regions_) {
        if (region.valid() && region.space == space) {
            return os::core::Result<ExecutableRegion>{region};
        }
    }
    // A space with nothing executable in it. Not a malformed space and not a
    // missing capability - there is simply nowhere for a thread to begin, and
    // saying so is what lets a loader tell "I have not mapped the text yet"
    // apart from "I am not allowed to do this".
    return os::core::Result<ExecutableRegion>{
        executable_region_error(executable_region_errors::not_executable)};
}

os::core::Result<void> ExecutableRegionTable::forget(
    AddressSpaceIdentity space) noexcept {
    if (!space.valid()) {
        return executable_region_error(executable_region_errors::invalid_space);
    }
    for (auto& region : regions_) {
        if (region.valid() && region.space == space) {
            region = ExecutableRegion{};
            --live_;
            return {};
        }
    }
    // Forgetting a space that had no executable region is not an error. A space
    // can be destroyed before anything was mapped into it, and making teardown
    // depend on how far construction got is how teardown paths acquire branches
    // that are never exercised.
    return {};
}

} // namespace os::kernel
