#pragma once

#include <cstdint>

#include <sys/types.h>

#include <os/core/result.hpp>

// Test-only resource measurement support.
//
// This deliberately lives under tests/ rather than core/. Budget enforcement is
// a build-time quality gate, not a runtime OS responsibility, and ENML keeps the
// shipped trusted surface as small as possible. Nothing here is public ABI.
namespace budget {

namespace errors {
inline constexpr std::uint32_t measurement_unavailable = 1;
inline constexpr std::uint32_t malformed_proc_status = 2;
inline constexpr std::uint32_t unknown_budget = 3;
inline constexpr std::uint32_t clock_unavailable = 4;
} // namespace errors

// A single point-in-time observation of one process, read from procfs.
//
// Context switch counts are the wakeup proxy: a correctly event-driven service
// that is blocked in poll()/epoll_wait() with nothing to do accrues no
// voluntary switches. Growth over an idle window means the process is waking up
// for work it should not be doing.
struct ProcessSample final {
    std::uint64_t resident_kib {0};
    std::uint64_t voluntary_switches {0};
    std::uint64_t involuntary_switches {0};

    [[nodiscard]] constexpr std::uint64_t total_switches() const noexcept {
        return voluntary_switches + involuntary_switches;
    }
};

// Reads /proc/<pid>/status. Bounded, allocation-free, and EINTR-safe.
[[nodiscard]] os::core::Result<ProcessSample> sample_process(pid_t pid) noexcept;

[[nodiscard]] os::core::Result<std::uint64_t> monotonic_ms() noexcept;

// Sleeps for the requested duration, resuming across signal interruption so a
// short idle window is not silently truncated.
void sleep_ms(std::uint64_t duration_ms) noexcept;

// True when procfs exposes the fields this harness depends on. Callers use this
// to skip cleanly rather than fail on a kernel/emulator that cannot report them.
[[nodiscard]] bool measurement_supported() noexcept;

} // namespace budget
