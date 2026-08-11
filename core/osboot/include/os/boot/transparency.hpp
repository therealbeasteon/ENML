#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/boot/sealing.hpp>
#include <os/core/result.hpp>

// Update transparency: proving an update was published, not just signed.
//
// M1.0 binds a package to its signer, which answers "did the holder of the key
// approve this?" It does not answer the question that matters to a project whose
// threat model already declines to trust the vendor - M7.4c refuses a group
// manager for attestation on exactly that ground. A signature is equally valid
// on a build made for everybody and on a build made for one person, and the
// second is what a targeted attack looks like. The key does not have to leak;
// its holder only has to be persuaded once.
//
// A Merkle log closes that. The signer publishes every release into an
// append-only tree, and the device refuses an update it cannot prove is *in*
// that tree. A targeted build then has two options, and both are bad for the
// attacker: publish it, where it is visible to anyone watching, or fail to
// install. Transparency does not prevent a malicious update - it removes the
// attacker's ability to deliver one quietly, which for a targeted attack is the
// same thing.
//
// The supplied Merkle references shape two decisions.
//
// **Path length is bounded, because path length is a security parameter.** The
// collision study finds "a direct correlation between the increase in path
// length and the heightened probability of root collisions", and that longer
// hashes reduce it. So the proof depth has a stated ceiling rather than being
// however long a caller says, and the digest is 256 bits rather than the
// smallest that would fit.
//
// **Leaves and internal nodes are hashed with different prefixes.** Without
// that, an internal node's two children can be presented as a leaf, and a proof
// for a value nobody ever published verifies against the same root. It is the
// oldest structural defect in Merkle constructions and it is invisible in a test
// that only checks that honest proofs pass.
//
// The condensation result in the other Merkle reference - post-quantum signature
// sizes of 666 to 49,856 bytes reduced to 248-472 by sending occasional
// reference values instead of a signature per message - is the reason this shape
// is affordable on a phone over a metered radio. It is not implemented here; it
// is why the log is the right structure to build against rather than a list.
namespace os::boot {

// Reuse of the measurement digest: 256 bits, and the same hash the boot
// measurements and the attestation design already need. Keeping the mandatory
// primitive count at one is a deliberate, repeated choice.
using LogDigest = MeasurementDigest;

// The ceiling on proof length, and therefore on log size: 2^40 entries is more
// releases than any product will ever have, and every step below that ceiling is
// a step the collision study says costs something.
inline constexpr std::size_t max_inclusion_path = 40U;

// Domain separation prefixes. One byte, distinct, and applied without exception.
inline constexpr std::uint8_t leaf_prefix = 0x00U;
inline constexpr std::uint8_t node_prefix = 0x01U;

namespace transparency_errors {
inline constexpr std::uint32_t invalid_tree = 1U;
inline constexpr std::uint32_t invalid_leaf_index = 2U;
// The supplied path is not the length the tree size implies. A proof whose
// length is taken on trust is a proof an attacker chooses the shape of.
inline constexpr std::uint32_t path_length_mismatch = 3U;
inline constexpr std::uint32_t path_too_long = 4U;
inline constexpr std::uint32_t root_mismatch = 5U;
inline constexpr std::uint32_t no_hash_provider = 6U;
} // namespace transparency_errors

// The hash is a provider, on the M2.4 split: policy here, primitive elsewhere.
// Taking it as a parameter is also what lets this be tested exhaustively without
// a cryptographic implementation present.
using HashFunction = void (*)(const std::uint8_t* data, std::size_t length, LogDigest& out);

struct InclusionProof final {
    std::uint64_t leaf_index {0U};
    std::uint64_t tree_size {0U};
    std::array<LogDigest, max_inclusion_path> path {};
    std::size_t path_length {0U};
};

// How long a proof must be for a leaf in a tree of this size.
//
// Computed rather than trusted. The caller supplies the path length, and a
// verifier that simply consumed what it was given would let an attacker pick a
// shorter path that happens to collide - which is the path-length finding in the
// collision study turned into an attack.
[[nodiscard]] os::core::Result<std::size_t> expected_path_length(
    std::uint64_t leaf_index,
    std::uint64_t tree_size) noexcept;

// Hashes a leaf with domain separation.
[[nodiscard]] os::core::Result<LogDigest> hash_leaf(
    HashFunction hash,
    const std::uint8_t* data,
    std::size_t length) noexcept;

// Verifies that `leaf` sits at `proof.leaf_index` in a tree of `proof.tree_size`
// whose root is `root`.
[[nodiscard]] os::core::Result<void> verify_inclusion(
    HashFunction hash,
    const LogDigest& leaf,
    const InclusionProof& proof,
    const LogDigest& root) noexcept;

} // namespace os::boot
