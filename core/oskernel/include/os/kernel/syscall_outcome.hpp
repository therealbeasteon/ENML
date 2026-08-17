#pragma once

#include <cstdint>

#include <os/core/error.hpp>
#include <os/kernel/aarch64_exception.hpp>
#include <os/kernel/abi.hpp>

// The kernel half of the syscall return convention. docs/M7_14_SYSCALL_ABI.md.
//
// M7.14 built the user half and nothing produced what it reads: the kernel
// wrote `frame->x[0] = 1` on one path and `0` on another and never touched the
// outcome register, so `os::abi::decode_outcome` would have reported "no
// answer" for every call Cookie has ever served. A decoder with no encoder is
// the same defect this project found twice in one day under a different name -
// a mechanism that is present is not a mechanism that is engaged.
//
// **Every result a call returns goes through here.** That is the point of the
// file rather than a style preference. A convention enforced by remembering is
// a convention with a hole in it per call site, and there are twenty-odd sites;
// the two functions below are the only places `outcome_register` is written, so
// the property "a served call answered" is checkable by reading one file.
namespace os::kernel::aarch64 {

// The call succeeded and returns no value.
//
// Result registers are cleared rather than left alone. A call that answers
// "yes" and leaves x0 holding its own first argument is handing the caller back
// its own input as though it were an answer, and the caller cannot tell.
void answer(ExceptionFrame& frame) noexcept;

// The call succeeded and returns one value in x0, or two in x0 and x1.
//
// Deliberately no three- or four-value overload. Nothing in the surface returns
// more than two, and an overload nobody calls is a shape somebody will later
// fill in without deciding whether it should exist.
void answer(ExceptionFrame& frame, std::uint64_t x0) noexcept;
void answer(ExceptionFrame& frame, std::uint64_t x0, std::uint64_t x1) noexcept;

// The call was refused, and this is why.
//
// The whole Error crosses - domain included - because it fits in a register
// exactly. The remaining result registers are cleared: a refusal that leaves
// stale values behind invites a caller which forgot to check the tag to read
// them as results, which is the failure mode the tag exists to make impossible.
void refuse(ExceptionFrame& frame, os::core::Error error) noexcept;

// Whether this frame carries an answer or a refusal.
//
// For the kernel's own assertions, not for dispatch. A path that has to ask
// whether it already answered is a path with two owners of that decision, and
// the fix is to have one - but a boot proof checking its own work is exactly
// the case where asking is right.
[[nodiscard]] bool answered(const ExceptionFrame& frame) noexcept;

// Clears the outcome so a frame cannot inherit the previous call's answer.
//
// The user stub zeroes x7 before its own `svc`, which covers a caller that uses
// the stub. This covers the rest: a thread entered from a frame the kernel
// built, and a redirect that reuses a frame which already answered. Both are
// how the boot proof drives EL0, so both are how a stale `answered` would
// actually arrive.
void clear_outcome(ExceptionFrame& frame) noexcept;

} // namespace os::kernel::aarch64
