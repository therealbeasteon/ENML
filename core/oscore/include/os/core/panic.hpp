#pragma once

#include <cstdlib>

namespace os::core {

// Immediate, unconditional termination on a broken internal invariant.
//
// ENML uses Result<T> for every recoverable failure. Reaching this function
// means something that cannot fail has failed anyway: a caller read a value out
// of an error Result, or read an error out of a success Result. At that point
// the program's own state model is already wrong, and the only safe action is
// to stop.
//
// This deliberately is not assert(). assert() compiles away under NDEBUG, which
// means the check disappears in exactly the builds that ship: a missed Result
// check would degrade from "trap" to "read the inactive union member and keep
// running on garbage". For a system whose first stated priority is security by
// default, an invariant guard that is present only in debug builds is not a
// guard. Enforcement must be identical in every build configuration.
//
// __builtin_trap() is preferred over abort(): it emits an illegal instruction
// rather than raising SIGABRT, so it cannot be intercepted by an installed
// signal handler and cannot be talked out of terminating.
//
// This function is intentionally not constexpr. A misuse detected during
// constant evaluation should be a compile error, which is strictly better than
// any runtime behavior.
[[noreturn]] inline void invariant_violated() noexcept {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_trap();
#else
    std::abort();
#endif
}

} // namespace os::core
