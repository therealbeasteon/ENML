#include <cstdint>
#include <cstdio>

#include <os/abi/syscall.hpp>
#include <os/core/error.hpp>
#include <os/kernel/aarch64_exception.hpp>
#include <os/kernel/abi.hpp>
#include <os/kernel/syscall_outcome.hpp>

// The two halves of the return convention, checked against each other.
//
// The kernel writes an outcome into an exception frame; userland reads one out
// of its registers. Neither is checked here against a restatement of
// docs/M7_14_SYSCALL_ABI.md - they are checked against each other, which is the
// only comparison that catches the failure that matters. An encoder and a
// decoder that were each written correctly against the document can still
// disagree; an encoder tested through its decoder cannot.

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "syscall outcome: %s\n", what);
    }
    return condition;
}

// What the user stub would read back out of a frame the kernel just wrote.
os::core::Result<os::abi::SyscallAnswer> read_back(
    const os::kernel::aarch64::ExceptionFrame& frame) {
    return os::abi::decode_outcome(
        frame.x[os::abi::outcome_register],
        {frame.x[0], frame.x[1], frame.x[2], frame.x[3]});
}

// A frame with every register dirty. Every case below starts from this rather
// than from zeros, because the properties being checked are about what the
// kernel *clears*, and a zeroed frame passes those vacuously.
os::kernel::aarch64::ExceptionFrame dirty_frame() {
    os::kernel::aarch64::ExceptionFrame frame{};
    for (std::size_t index = 0U; index < frame.x.size(); ++index) {
        frame.x[index] = 0xDEAD'0000'0000'0000ULL | index;
    }
    return frame;
}

} // namespace

int main() {
    // --- The two halves agree on where the outcome lives and what it says.
    // A static check, because a divergence here is a build-time fact rather
    // than something worth discovering at run time.
    static_assert(os::abi::outcome_answered == os::kernel::outcome_answered);
    static_assert(os::abi::outcome_refused == os::kernel::outcome_refused);
    static_assert(os::abi::outcome_register == os::kernel::outcome_register);

    // --- An answer with no value.
    {
        auto frame = dirty_frame();
        os::kernel::aarch64::answer(frame);
        auto seen = read_back(frame);
        if (!check(static_cast<bool>(seen), "a valueless answer read as a refusal")) return 1;
        for (std::size_t index = 0U; index < os::kernel::max_call_arguments; ++index) {
            if (!check(seen.value().results[index] == 0ULL,
                       "a valueless answer left a dirty result register - the "
                       "caller gets its own argument back as an answer")) return 1;
        }
    }

    // --- An answer with one value, and with two.
    {
        auto frame = dirty_frame();
        os::kernel::aarch64::answer(frame, 0x5EA1'0000'0000'00A1ULL);
        auto seen = read_back(frame);
        if (!check(static_cast<bool>(seen), "a one-value answer read as a refusal")) return 1;
        if (!check(seen.value().results[0] == 0x5EA1'0000'0000'00A1ULL,
                   "a one-value answer lost its value")) return 1;
        if (!check(seen.value().results[1] == 0ULL && seen.value().results[2] == 0ULL &&
                       seen.value().results[3] == 0ULL,
                   "a one-value answer left later registers dirty")) return 1;
    }
    {
        auto frame = dirty_frame();
        os::kernel::aarch64::answer(frame, 0x1111ULL, 0x2222ULL);
        auto seen = read_back(frame);
        if (!check(static_cast<bool>(seen), "a two-value answer read as a refusal")) return 1;
        if (!check(seen.value().results[0] == 0x1111ULL && seen.value().results[1] == 0x2222ULL,
                   "a two-value answer lost a value or transposed them")) return 1;
        if (!check(seen.value().results[2] == 0ULL && seen.value().results[3] == 0ULL,
                   "a two-value answer left later registers dirty")) return 1;
    }

    // --- A refusal carries the whole Error, domain included.
    {
        for (const os::core::Error reason :
             {os::core::Error{os::core::ErrorDomain::kernel, 0U, 260U},
              os::core::Error{os::core::ErrorDomain::image, 0U, 0xFFFF'FFFFU},
              os::core::Error{os::core::ErrorDomain::core, 0U, 1U}}) {
            auto frame = dirty_frame();
            os::kernel::aarch64::refuse(frame, reason);
            auto seen = read_back(frame);
            if (!check(!seen, "a refusal read as an answer")) return 1;
            if (!check(seen.error() == reason,
                       "a refusal did not arrive as the Error the kernel "
                       "refused with")) return 1;
        }
    }

    // --- The case the whole convention exists for.
    //
    // A kernel path that returns without answering leaves the outcome register
    // as the caller zeroed it. That must not read as success - and it must not
    // read as a refusal either, because a refusal is an answer and this is the
    // absence of one.
    {
        auto frame = dirty_frame();
        os::kernel::aarch64::clear_outcome(frame);
        if (!check(!os::kernel::aarch64::answered(frame),
                   "a cleared outcome reported as answered")) return 1;
        auto seen = read_back(frame);
        if (!check(!seen, "an unanswered call read as an answer")) return 1;
        if (!check(seen.error().code == os::abi::abi_errors::no_answer,
                   "an unanswered call was reported as something other than "
                   "'no answer'")) return 1;
    }

    // --- A frame that already answered does not silently carry that answer
    // into the next call. This is how a stale success would really arrive: the
    // boot proof reuses frames to redirect a thread into another syscall.
    {
        auto frame = dirty_frame();
        os::kernel::aarch64::answer(frame, 1ULL);
        if (!check(os::kernel::aarch64::answered(frame),
                   "an answered frame did not report as answered")) return 1;
        os::kernel::aarch64::clear_outcome(frame);
        auto seen = read_back(frame);
        if (!check(!seen, "a reused frame kept its previous answer")) return 1;
        if (!check(seen.error().code == os::abi::abi_errors::no_answer,
                   "a reused frame's stale outcome was misreported")) return 1;
    }

    // --- answered() agrees with the decoder about both tags, and about
    // nothing else being one.
    {
        auto frame = dirty_frame();
        os::kernel::aarch64::refuse(frame, os::core::Error{os::core::ErrorDomain::kernel, 0U, 1U});
        if (!check(os::kernel::aarch64::answered(frame),
                   "a refusal did not report as answered - a refusal is an "
                   "answer")) return 1;

        frame.x[os::kernel::outcome_register] = os::kernel::outcome_answered ^ 1ULL;
        if (!check(!os::kernel::aarch64::answered(frame),
                   "a value one bit from a tag reported as an outcome")) return 1;
        auto seen = read_back(frame);
        if (!check(!seen, "a value one bit from a tag decoded as an answer")) return 1;
    }

    std::fprintf(stderr, "syscall outcome: kernel writer and user reader agree\n");
    return 0;
}
