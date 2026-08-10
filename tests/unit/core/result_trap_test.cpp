#include <cstdint>

#include <sys/wait.h>
#include <unistd.h>

#include <os/core/result.hpp>

// Proves that reading the wrong alternative of a Result terminates the process
// in every build configuration.
//
// This is a security property, not a style preference. Result<T> carries every
// recoverable failure in ENML, so a missed check must fail closed rather than
// return the inactive union member. A guard that only exists under a debug
// build is not a guard, and the only way to know the guard is real is to
// observe a process actually die on misuse.

using namespace os::core;

namespace {

enum class Misuse : std::uint8_t {
    value_on_error,
    error_on_value,
    void_value_on_error,
    void_error_on_value,
};

// Consuming the result through a volatile sink stops the optimizer from
// discarding the misuse as a dead read.
volatile int sink = 0;

// The branch is driven through a volatile read so the compiler cannot fold the
// Result into a known alternative and reason the trap away at compile time.
volatile bool always_true = true;

void perform_misuse(Misuse misuse) {
    const Error error = core_error(errors::core::invalid_argument);

    switch (misuse) {
    case Misuse::value_on_error: {
        Result<int> result = always_true ? Result<int>{error} : Result<int>{1};
        sink = result.value();
        break;
    }
    case Misuse::error_on_value: {
        Result<int> result = always_true ? Result<int>{1} : Result<int>{error};
        sink = static_cast<int>(result.error().code);
        break;
    }
    case Misuse::void_value_on_error: {
        Result<void> result = always_true ? Result<void>{error} : Result<void>{};
        result.value();
        sink = 1;
        break;
    }
    case Misuse::void_error_on_value: {
        Result<void> result = always_true ? Result<void>{} : Result<void>{error};
        sink = static_cast<int>(result.error().code);
        break;
    }
    }
}

// Returns true when the misuse terminated the child. Any abnormal termination
// counts: __builtin_trap() raises SIGILL on the supported toolchains, but the
// property under test is that the process does not survive and continue on
// garbage, not which signal delivers that outcome.
[[nodiscard]] bool misuse_terminates_process(Misuse misuse) {
    const pid_t child = ::fork();
    if (child < 0) {
        return false;
    }

    if (child == 0) {
        perform_misuse(misuse);
        // Reaching here means the guard did not fire. Exit 0 is the single
        // outcome the parent treats as failure.
        ::_exit(0);
    }

    int status = 0;
    pid_t waited = -1;
    do {
        waited = ::waitpid(child, &status, 0);
    } while (waited < 0);

    const bool survived = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    return !survived;
}

} // namespace

// Deliberately not assert(): a test whose subject is "this guard survives
// NDEBUG" must not itself be compiled out under NDEBUG.
int main() {
    const Misuse cases[] {
        Misuse::value_on_error,
        Misuse::error_on_value,
        Misuse::void_value_on_error,
        Misuse::void_error_on_value,
    };

    for (const auto misuse : cases) {
        if (!misuse_terminates_process(misuse)) {
            return 1;
        }
    }
    return 0;
}
