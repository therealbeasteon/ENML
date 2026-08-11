#pragma once

#include <cstddef>

#include <os/core/span.hpp>

// Primitives for handling values whose contents must not leak through timing or
// through memory that outlives them.
//
// Two ordinary-looking C++ constructs are wrong for secrets, and both look
// correct in review:
//
//   - `a == b` on a byte array short-circuits at the first differing byte, so
//     how long it takes reveals how much of the value the caller got right. An
//     attacker who can submit candidates and time the answer recovers the value
//     one byte at a time, turning an exponential search into a linear one.
//
//   - A loop that zeroes a buffer just before it goes out of scope has no
//     observable effect by the language's rules, so a compiler is entitled to
//     delete it entirely. The secret then stays in memory - in a stack frame
//     that will be reused, in a page that may be swapped or dumped - after the
//     code that was supposed to erase it.
//
// The cache-attack literature is a reminder of how little signal an attacker
// needs. Recovering a full AES key from a phone by timing cache hits requires
// no privileges at all; it just needs the victim's memory access pattern to
// depend on the secret. Timing-dependent *comparison* is the same class of
// mistake with a much shorter path from observation to key.
//
// A related rule that has no code here because it is a prohibition: a
// production cipher implementation must not use secret-indexed lookup tables.
// The classic table-driven AES construction is exactly the target those attacks
// were built for. Use the CPU's cryptographic instructions where the platform
// has them, and a bitsliced or otherwise data-independent implementation where
// it does not.
namespace os::core {

// Compares two byte spans in time that does not depend on their contents.
//
// Every byte of both spans is examined; there is no early exit, and the
// accumulated difference passes through a barrier so the compiler may not
// reintroduce one. Returns false for differing lengths, which is not a leak:
// a length is a property of the protocol and is already known to anyone who
// can see the message.
[[nodiscard]] bool constant_time_equal(ByteSpan left, ByteSpan right) noexcept;

// Overwrites a buffer with zeroes in a way the compiler may not remove.
//
// Call this on anything holding key material, a nonce, a tag or a plaintext
// before it goes out of scope. Being unable to prove a secret was erased is
// indistinguishable from not having erased it.
void secure_zero(MutableByteSpan bytes) noexcept;

} // namespace os::core
