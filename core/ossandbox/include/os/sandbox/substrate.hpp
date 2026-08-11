#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/sandbox/sandbox.hpp>

// What the kernel underneath ENML actually provides.
//
// Every other layer in this tree already represents the assurance its platform
// supplies, and refuses claims the platform cannot back: the boot state carries
// a root-of-trust capability set, a device access policy will not call a driver
// confined when its device masters the bus without an IOMMU, and the partition
// ledger will not hand out isolation a platform has no mechanism for.
//
// The substrate - the layer all of that stands on - had no such representation.
// A sandbox policy could ask for seccomp filtering and Landlock confinement,
// and whether the running kernel offered either was discovered in the child,
// after fork, at the moment of applying it. That fails closed, which is
// correct, but it means the answer to "can this system confine its services"
// was not knowable until something tried.
//
// This makes it knowable, and keeps the same rule as everywhere else: a policy
// asking for a facility the substrate does not have is refused, never quietly
// downgraded to one that runs unconfined.
namespace os::sandbox {

// Zero is never a valid single capability.
enum class SubstrateCapability : std::uint32_t {
    // prctl(PR_SET_NO_NEW_PRIVS). Without it, execve can regain privilege and
    // every other confinement below is bypassable by exec.
    no_new_privs = 1U << 0U,
    // Seccomp syscall filtering.
    seccomp_filter = 1U << 1U,
    // Landlock filesystem confinement.
    landlock = 1U << 2U,
    // Per-process resource limits, which the resource budget gate depends on.
    resource_limits = 1U << 3U,
    // A datagram socket preserving message boundaries and carrying
    // kernel-attested peer credentials. Every typed identity in ENML rests on
    // the credentials coming from the kernel rather than from the peer, so
    // this is the one capability whose absence is not a degradation but a
    // different operating system.
    attested_datagram_ipc = 1U << 4U,
};

inline constexpr std::uint32_t known_substrate_capabilities =
    static_cast<std::uint32_t>(SubstrateCapability::no_new_privs) |
    static_cast<std::uint32_t>(SubstrateCapability::seccomp_filter) |
    static_cast<std::uint32_t>(SubstrateCapability::landlock) |
    static_cast<std::uint32_t>(SubstrateCapability::resource_limits) |
    static_cast<std::uint32_t>(SubstrateCapability::attested_datagram_ipc);

namespace substrate_errors {
inline constexpr std::uint32_t unknown_capability = 1U;
inline constexpr std::uint32_t no_new_privs_unavailable = 2U;
inline constexpr std::uint32_t seccomp_unavailable = 3U;
inline constexpr std::uint32_t landlock_unavailable = 4U;
inline constexpr std::uint32_t resource_limits_unavailable = 5U;
inline constexpr std::uint32_t attested_ipc_unavailable = 6U;
} // namespace substrate_errors

// What the running kernel offers.
//
// A default report offers nothing, so a caller that forgets to probe cannot
// conclude a policy is satisfiable.
class SubstrateReport final {
public:
    SubstrateReport() noexcept = default;

    // Unknown bits are rejected, as with every other discriminant here.
    [[nodiscard]] static os::core::Result<SubstrateReport> create(
        std::uint32_t capabilities) noexcept;

    [[nodiscard]] std::uint32_t capabilities() const noexcept { return capabilities_; }

    [[nodiscard]] bool provides(SubstrateCapability capability) const noexcept {
        return (capabilities_ & static_cast<std::uint32_t>(capability)) != 0U;
    }

    // Whether this policy can be honoured exactly as written.
    //
    // Named for what it answers rather than returning a bool, so the error
    // says which facility is missing - a caller that cannot start a service
    // should be able to report why without guessing.
    //
    // A disabled policy is satisfiable by any substrate: it asks for nothing.
    // That is deliberate and is not a loophole, because a service running under
    // a disabled sandbox is not claiming to be confined.
    [[nodiscard]] os::core::Result<void> satisfies(const SandboxPolicyV1& policy) const noexcept;

private:
    std::uint32_t capabilities_ {0};
};

// Probes the running kernel.
//
// Each probe is a real attempt to use the facility rather than a version check:
// a kernel that reports a feature and refuses it is more dangerous than one
// that lacks it, since the first produces a service that believes it is
// confined.
[[nodiscard]] os::core::Result<SubstrateReport> probe_substrate() noexcept;

} // namespace os::sandbox
