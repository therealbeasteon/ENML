#include <cstdint>
#include <cstdio>

#include <os/core/error.hpp>
#include <os/sandbox/substrate.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "substrate: %s\n", what);
    }
    return condition;
}

bool refused(const os::core::Result<void>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::security &&
        result.error().code == code;
}

constexpr std::uint32_t bit(os::sandbox::SubstrateCapability capability) {
    return static_cast<std::uint32_t>(capability);
}

os::sandbox::SubstrateReport report_of(std::uint32_t capabilities) {
    auto created = os::sandbox::SubstrateReport::create(capabilities);
    return created ? created.value() : os::sandbox::SubstrateReport{};
}

constexpr os::sandbox::SandboxPolicyV1 strict{
    .enabled = true,
    .require_no_new_privs = true,
    .clear_capabilities = true,
    .require_seccomp = true,
    .require_landlock = true,
};

} // namespace

int main() {
    // A default report offers nothing, so a caller that forgets to probe
    // cannot conclude a policy is satisfiable.
    {
        const os::sandbox::SubstrateReport defaulted{};
        if (!check(defaulted.capabilities() == 0U, "default report claimed capabilities")) return 1;
        if (!check(!defaulted.satisfies(strict), "default report satisfied a strict policy")) return 1;
    }

    // Unknown bits are rejected like every other discriminant.
    if (!check(!os::sandbox::SubstrateReport::create(0x8000'0000U),
               "unknown capability bit accepted")) return 1;

    const auto everything = report_of(os::sandbox::known_substrate_capabilities);
    if (!check(static_cast<bool>(everything.satisfies(strict)),
               "complete substrate refused a strict policy")) return 1;

    // Each missing facility is named, so a caller that cannot start a service
    // can report why rather than guessing.
    {
        const auto without_nnp = report_of(
            os::sandbox::known_substrate_capabilities & ~bit(
                os::sandbox::SubstrateCapability::no_new_privs));
        if (!check(refused(without_nnp.satisfies(strict),
                           os::sandbox::substrate_errors::no_new_privs_unavailable),
                   "missing no_new_privs not reported")) return 1;

        const auto without_seccomp = report_of(
            os::sandbox::known_substrate_capabilities & ~bit(
                os::sandbox::SubstrateCapability::seccomp_filter));
        if (!check(refused(without_seccomp.satisfies(strict),
                           os::sandbox::substrate_errors::seccomp_unavailable),
                   "missing seccomp not reported")) return 1;

        const auto without_landlock = report_of(
            os::sandbox::known_substrate_capabilities & ~bit(
                os::sandbox::SubstrateCapability::landlock));
        if (!check(refused(without_landlock.satisfies(strict),
                           os::sandbox::substrate_errors::landlock_unavailable),
                   "missing landlock not reported")) return 1;

        const auto without_limits = report_of(
            os::sandbox::known_substrate_capabilities & ~bit(
                os::sandbox::SubstrateCapability::resource_limits));
        if (!check(refused(without_limits.satisfies(strict),
                           os::sandbox::substrate_errors::resource_limits_unavailable),
                   "missing resource limits not reported")) return 1;

        // no_new_privs is reported first even when more than one facility is
        // missing, because without it an execve regains privilege and defeats
        // whatever else was applied.
        const auto bare = report_of(bit(os::sandbox::SubstrateCapability::attested_datagram_ipc));
        if (!check(refused(bare.satisfies(strict),
                           os::sandbox::substrate_errors::no_new_privs_unavailable),
                   "most defeating absence not reported first")) return 1;
    }

    // A policy asking for less is satisfied by a substrate offering less. The
    // rule is that a request is never downgraded, not that every kernel must
    // offer everything.
    {
        constexpr os::sandbox::SandboxPolicyV1 modest{
            .enabled = true,
            .require_no_new_privs = true,
            .clear_capabilities = true,
            .require_seccomp = true,
            .require_landlock = false,
        };
        const auto without_landlock = report_of(
            os::sandbox::known_substrate_capabilities & ~bit(
                os::sandbox::SubstrateCapability::landlock));
        if (!check(static_cast<bool>(without_landlock.satisfies(modest)),
                   "policy not requiring landlock refused for lacking it")) return 1;
    }

    // A disabled policy asks for nothing and is satisfiable anywhere. This is
    // not a loophole: a service under a disabled sandbox is not claiming to be
    // confined, and pretending otherwise is what this whole type prevents.
    {
        constexpr os::sandbox::SandboxPolicyV1 disabled{.enabled = false};
        const os::sandbox::SubstrateReport nothing{};
        if (!check(static_cast<bool>(nothing.satisfies(disabled)),
                   "disabled policy refused")) return 1;
    }

    // The probe itself must succeed and must not invent capabilities. What the
    // CI kernel actually offers is not asserted - Landlock in particular is a
    // legitimate absence, and a gate that demanded it would be asserting the
    // runner's configuration rather than ENML's behaviour.
    {
        auto probed = os::sandbox::probe_substrate();
        if (!check(static_cast<bool>(probed), "probe failed")) return 1;
        if (!check((probed.value().capabilities() & ~os::sandbox::known_substrate_capabilities) == 0U,
                   "probe reported an unknown capability")) return 1;
        // Any Linux ENML can run on has these two; their absence would mean
        // the typed-identity model does not work at all here.
        if (!check(probed.value().provides(os::sandbox::SubstrateCapability::resource_limits),
                   "probe found no resource limits")) return 1;
        if (!check(probed.value().provides(
                       os::sandbox::SubstrateCapability::attested_datagram_ipc),
                   "probe found no attested datagram IPC")) return 1;
    }

    return 0;
}
