#include <budget/measure.hpp>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <time.h>
#include <unistd.h>

namespace budget {
namespace {

[[nodiscard]] constexpr os::core::Error budget_error(std::uint32_t code) noexcept {
    // Measurement failures are core-domain: this harness is not an IPC peer and
    // must not manufacture ipc-domain errors that look like protocol faults.
    return os::core::make_error(os::core::ErrorDomain::core, code);
}

// /proc/<pid>/status is a few hundred bytes on Linux. The buffer is generously
// oversized and the read is still explicitly bounded and truncation-aware.
inline constexpr std::size_t status_buffer_size = 8192U;

struct StatusText final {
    char bytes[status_buffer_size] {};
    std::size_t length {0};
};

[[nodiscard]] os::core::Result<void>
read_proc_status(pid_t pid, StatusText& output) noexcept {
    char path[64] {};
    const int written = std::snprintf(
        path, sizeof(path), "/proc/%lld/status", static_cast<long long>(pid));
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(path)) {
        return budget_error(errors::measurement_unavailable);
    }

    int fd = -1;
    do {
        fd = ::open(path, O_RDONLY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);

    if (fd < 0) {
        return budget_error(errors::measurement_unavailable);
    }

    std::size_t total = 0;
    while (total < sizeof(output.bytes) - 1U) {
        const auto received =
            ::read(fd, output.bytes + total, sizeof(output.bytes) - 1U - total);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            ::close(fd);
            return budget_error(errors::measurement_unavailable);
        }
        if (received == 0) {
            break;
        }
        total += static_cast<std::size_t>(received);
    }
    ::close(fd);

    output.bytes[total] = '\0';
    output.length = total;
    return {};
}

// Matches `prefix` only at a line start, then parses the first unsigned decimal
// run that follows. Anchoring matters: "voluntary_ctxt_switches" is a suffix of
// "nonvoluntary_ctxt_switches", so an unanchored search reports the wrong field.
[[nodiscard]] bool
parse_field(const StatusText& text, const char* prefix, std::uint64_t& output) noexcept {
    const auto prefix_length = std::strlen(prefix);
    std::size_t index = 0;

    while (index < text.length) {
        const bool at_line_start = (index == 0U) || (text.bytes[index - 1U] == '\n');
        if (at_line_start &&
            (text.length - index) >= prefix_length &&
            std::memcmp(text.bytes + index, prefix, prefix_length) == 0) {
            std::size_t cursor = index + prefix_length;

            while (cursor < text.length &&
                   (text.bytes[cursor] == ' ' || text.bytes[cursor] == '\t')) {
                ++cursor;
            }

            bool any_digit = false;
            std::uint64_t value = 0;
            while (cursor < text.length &&
                   text.bytes[cursor] >= '0' && text.bytes[cursor] <= '9') {
                const auto digit = static_cast<std::uint64_t>(text.bytes[cursor] - '0');
                // Guard the accumulate so a hostile/corrupt procfs value cannot
                // silently wrap into a small number that passes a budget.
                if (value > (UINT64_MAX - digit) / 10U) {
                    return false;
                }
                value = (value * 10U) + digit;
                any_digit = true;
                ++cursor;
            }

            if (!any_digit) {
                return false;
            }
            output = value;
            return true;
        }
        ++index;
    }
    return false;
}

} // namespace

os::core::Result<ProcessSample> sample_process(pid_t pid) noexcept {
    StatusText text {};
    auto read_result = read_proc_status(pid, text);
    if (!read_result) {
        return read_result.error();
    }

    ProcessSample sample {};
    if (!parse_field(text, "VmRSS:", sample.resident_kib) ||
        !parse_field(text, "voluntary_ctxt_switches:", sample.voluntary_switches) ||
        !parse_field(text, "nonvoluntary_ctxt_switches:", sample.involuntary_switches)) {
        return budget_error(errors::malformed_proc_status);
    }

    return sample;
}

os::core::Result<std::uint64_t> monotonic_ms() noexcept {
    timespec now {};
    if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return budget_error(errors::clock_unavailable);
    }
    return (static_cast<std::uint64_t>(now.tv_sec) * 1000ULL) +
        (static_cast<std::uint64_t>(now.tv_nsec) / 1000000ULL);
}

void sleep_ms(std::uint64_t duration_ms) noexcept {
    timespec remaining {
        .tv_sec = static_cast<time_t>(duration_ms / 1000ULL),
        .tv_nsec = static_cast<long>((duration_ms % 1000ULL) * 1000000ULL),
    };

    timespec pending {};
    while (::nanosleep(&remaining, &pending) != 0 && errno == EINTR) {
        remaining = pending;
    }
}

bool measurement_supported() noexcept {
    return static_cast<bool>(sample_process(::getpid()));
}

} // namespace budget
