#pragma once

#include <cstdint>
#include <string_view>

#include <os/core/result.hpp>

namespace budget {

// A declarative, reviewable resource ceiling for one supervised service.
//
// Budgets are compiled in rather than parsed from a config file at runtime. That
// matches the existing ENML rule against runtime YAML/JSON/XML in trusted
// components, and it makes a budget change show up as a reviewed source diff
// instead of quietly loosening in an untracked file.
struct ResourceBudget final {
    std::string_view name {};

    // Supervisor launch identity. Carried in the table rather than hardcoded in
    // the harness so measuring an additional service is a data change, not a
    // code change. These values only have to be internally consistent for the
    // measurement run; they are not the service's production identity.
    std::uint32_t service_id {0};
    std::uint64_t principal_high {0};
    std::uint64_t principal_low {0};

    // Peak resident set of the service process once it reports READY.
    std::uint64_t max_resident_kib {0};

    // Wall-clock from spawn to READY. This is the per-service contribution to
    // boot-to-shell time.
    std::uint64_t max_ready_ms {0};

    // Context switches observed across the idle window, expressed per second.
    // An event-driven service with no work should approach zero; a nonzero
    // floor here means something is polling.
    std::uint64_t max_idle_wakeups_per_second {0};

    // How long to hold the service idle while sampling.
    std::uint64_t idle_window_ms {1000};

    // Some services refuse to start without a durable state directory. When
    // set, the harness stages a temporary one and passes its descriptor through
    // the Supervisor's private launch channel, the same way App Manager does in
    // production. The service still never learns a path.
    bool requires_state_directory {false};
};

// Baselines are set from measured behavior with deliberate headroom, not from
// aspiration. A service that regresses past its ceiling fails CI; a service that
// improves should have its ceiling lowered in the same change that earns it,
// otherwise the gate rots upward.
// The trusted-system principal prefix used by supervised services, matching the
// shape the existing supervisor integration tests use.
inline constexpr std::uint64_t system_principal_high = 0x53595354454D0000ULL;

inline constexpr ResourceBudget service_budgets[] {
    // system.echo is the minimal ENML service: bootstrap, identity publication
    // and a bounded request loop with no storage, keys or UI. Its numbers are
    // therefore the floor cost of being an ENML service at all, and the most
    // sensitive early warning that the substrate itself is getting heavier.
    ResourceBudget{
        .name = "system.echo",
        .service_id = 0x0000F001U,
        .principal_high = system_principal_high,
        .principal_low = 0x000000000000F001ULL,
        // Measured 3516 KiB (x86-64) / 3012 KiB (AArch64), ready in 2 ms,
        // 0 idle wakeups. Ceilings carry deliberate headroom over the higher
        // architecture, not over the number we wish we had.
        .max_resident_kib = 4096U,
        .max_ready_ms = 250U,
        .max_idle_wakeups_per_second = 5U,
        .idle_window_ms = 1000U,
    },
    // system.storage is the first service with real product responsibility:
    // identity registry, private root registry and typed object capabilities.
    // Measured against echo it isolates what the storage substrate itself
    // costs, which is the number that matters as more services adopt the same
    // shape.
    ResourceBudget{
        .name = "system.storage",
        .service_id = 0x0000F020U,
        .principal_high = system_principal_high,
        .principal_low = 0x000000000000F020ULL,
        // Measured 3572 KiB (x86-64) / 3088 KiB (AArch64), ready in 1 ms,
        // 0 idle wakeups after the dispatch loop was fixed to block.
        .max_resident_kib = 4608U,
        .max_ready_ms = 250U,
        .max_idle_wakeups_per_second = 5U,
        .idle_window_ms = 1000U,
    },
    // system.keys is the heaviest supervised service: an OpenSSL-backed
    // provider, a key hierarchy and a durable registry all initialize before it
    // reports READY. Its ceilings are therefore expected to sit above the other
    // two, and its ready_ms is the one most worth watching as hierarchy work
    // grows. Only built when the host/CI provider is enabled.
    ResourceBudget{
        .name = "system.keys",
        .service_id = 0x0000F030U,
        .principal_high = system_principal_high,
        .principal_low = 0x000000000000F030ULL,
        // Measured 5264 KiB (x86-64) / 4744 KiB (AArch64), ready in 2 ms,
        // 0 idle wakeups. The provider, hierarchy and durable registry cost
        // roughly 1.7 MiB over a bare service, and initialize fast enough not
        // to register against the shared 250 ms startup ceiling.
        .max_resident_kib = 6656U,
        .max_ready_ms = 250U,
        .max_idle_wakeups_per_second = 5U,
        .idle_window_ms = 1000U,
        .requires_state_directory = true,
    },
};

[[nodiscard]] os::core::Result<ResourceBudget>
find_budget(std::string_view name) noexcept;

} // namespace budget
