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
};

// Baselines are set from measured behavior with deliberate headroom, not from
// aspiration. A service that regresses past its ceiling fails CI; a service that
// improves should have its ceiling lowered in the same change that earns it,
// otherwise the gate rots upward.
inline constexpr ResourceBudget service_budgets[] {
    // system.echo is the minimal ENML service: bootstrap, identity publication
    // and a bounded request loop with no storage, keys or UI. Its numbers are
    // therefore the floor cost of being an ENML service at all, and the most
    // sensitive early warning that the substrate itself is getting heavier.
    ResourceBudget{
        .name = "system.echo",
        .max_resident_kib = 4096U,
        .max_ready_ms = 750U,
        .max_idle_wakeups_per_second = 5U,
        .idle_window_ms = 1000U,
    },
};

[[nodiscard]] os::core::Result<ResourceBudget>
find_budget(std::string_view name) noexcept;

} // namespace budget
