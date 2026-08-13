#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <os/boot/transparency.hpp>
#include <os/core/error.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "transparency: %s\n", what);
    }
    return condition;
}

template <typename T>
bool refused(const os::core::Result<T>& r, std::uint32_t code) {
    return !r && r.error().domain == os::core::ErrorDomain::security && r.error().code == code;
}

// A deterministic test hash. Not cryptographic and not pretending to be - the
// point of taking the hash as a provider is that this logic can be exercised
// exhaustively without one.
void test_hash(const std::uint8_t* data, std::size_t length, os::boot::LogDigest& out) {
    std::uint64_t acc = 0xcbf29ce484222325ULL;
    for (std::size_t i = 0U; i < length; ++i) {
        acc ^= data[i];
        acc *= 0x100000001b3ULL;
    }
    for (std::size_t i = 0U; i < out.size(); ++i) {
        acc ^= (acc >> 33U);
        acc *= 0xff51afd7ed558ccdULL;
        out[i] = static_cast<std::uint8_t>(acc >> 56U);
    }
}

using os::boot::InclusionProof;
using os::boot::LogDigest;

// Builds a log the same way the verifier walks it, so proofs can be generated.
LogDigest root_of(const std::vector<LogDigest>& leaves, std::size_t begin, std::size_t size) {
    if (size == 1U) return leaves[begin];
    std::size_t split = 1U;
    while ((split << 1U) < size) split <<= 1U;
    const LogDigest left = root_of(leaves, begin, split);
    const LogDigest right = root_of(leaves, begin + split, size - split);
    std::array<std::uint8_t, 1U + 64U> buffer{};
    buffer[0] = os::boot::node_prefix;
    for (std::size_t i = 0U; i < 32U; ++i) {
        buffer[1U + i] = left[i];
        buffer[33U + i] = right[i];
    }
    LogDigest out{};
    test_hash(buffer.data(), buffer.size(), out);
    return out;
}

void path_for(
    const std::vector<LogDigest>& leaves,
    std::size_t begin,
    std::size_t size,
    std::size_t index,
    InclusionProof& proof) {
    if (size == 1U) return;
    std::size_t split = 1U;
    while ((split << 1U) < size) split <<= 1U;
    // Recurse first, then push: the audit path is ordered bottom-up, so the
    // leaf's own sibling has to end up at index zero.
    if (index < split) {
        path_for(leaves, begin, split, index, proof);
        proof.path[proof.path_length++] = root_of(leaves, begin + split, size - split);
    } else {
        path_for(leaves, begin + split, size - split, index - split, proof);
        proof.path[proof.path_length++] = root_of(leaves, begin, split);
    }
}

} // namespace

int main() {
    // Every leaf of every log size up to 33 proves, and the proof for a leaf
    // never proves a different one. Exhaustive because the tree shape for an
    // append-only log is irregular and the off-by-one lives in the odd sizes.
    for (std::size_t size = 1U; size <= 33U; ++size) {
        std::vector<LogDigest> leaves(size);
        for (std::size_t i = 0U; i < size; ++i) {
            const auto value = static_cast<std::uint8_t>(i + 1U);
            auto leaf = os::boot::hash_leaf(test_hash, &value, 1U);
            if (!check(static_cast<bool>(leaf), "hashing a leaf failed")) return 1;
            leaves[i] = leaf.value();
        }
        const LogDigest root = root_of(leaves, 0U, size);

        for (std::size_t i = 0U; i < size; ++i) {
            InclusionProof proof{};
            proof.leaf_index = i;
            proof.tree_size = size;
            path_for(leaves, 0U, size, i, proof);

            if (!check(static_cast<bool>(
                           os::boot::verify_inclusion(test_hash, leaves[i], proof, root)),
                       "an honest inclusion proof was refused")) return 1;

            // The same proof must not prove a neighbour.
            const std::size_t other = (i + 1U) % size;
            if (size > 1U &&
                !check(!static_cast<bool>(
                           os::boot::verify_inclusion(test_hash, leaves[other], proof, root)),
                       "a proof verified for the wrong leaf")) return 1;
        }
    }

    // Domain separation. Leaf and node hashes of identical bytes must differ, or
    // an internal node's children can be presented as a leaf and a proof
    // verifies for a value nobody published.
    {
        std::array<std::uint8_t, 64U> payload{};
        for (std::size_t i = 0U; i < payload.size(); ++i) {
            payload[i] = static_cast<std::uint8_t>(i);
        }
        auto as_leaf = os::boot::hash_leaf(test_hash, payload.data(), payload.size());
        if (!check(static_cast<bool>(as_leaf), "leaf hashing failed")) return 1;

        std::array<std::uint8_t, 1U + 64U> node_buffer{};
        node_buffer[0] = os::boot::node_prefix;
        for (std::size_t i = 0U; i < payload.size(); ++i) node_buffer[1U + i] = payload[i];
        LogDigest as_node{};
        test_hash(node_buffer.data(), node_buffer.size(), as_node);

        if (!check(as_leaf.value() != as_node,
                   "a leaf and an internal node hash the same bytes identically")) return 1;
    }

    // The path length is computed, not trusted. A proof of the wrong length is
    // refused before any hashing happens.
    {
        std::vector<LogDigest> leaves(8U);
        for (std::size_t i = 0U; i < 8U; ++i) {
            const auto v = static_cast<std::uint8_t>(i + 1U);
            leaves[i] = os::boot::hash_leaf(test_hash, &v, 1U).value();
        }
        const LogDigest root = root_of(leaves, 0U, 8U);

        InclusionProof proof{};
        proof.leaf_index = 3U;
        proof.tree_size = 8U;
        path_for(leaves, 0U, 8U, 3U, proof);
        proof.path_length -= 1U;
        if (!check(refused(os::boot::verify_inclusion(test_hash, leaves[3], proof, root),
                           os::boot::transparency_errors::path_length_mismatch),
                   "a short proof was accepted")) return 1;
    }

    // Malformed inputs are refused rather than interpreted.
    {
        if (!check(refused(os::boot::expected_path_length(0U, 0U),
                           os::boot::transparency_errors::invalid_tree),
                   "an empty tree was accepted")) return 1;
        if (!check(refused(os::boot::expected_path_length(5U, 5U),
                           os::boot::transparency_errors::invalid_leaf_index),
                   "a leaf index past the end was accepted")) return 1;

        InclusionProof proof{};
        proof.leaf_index = 0U;
        proof.tree_size = 1U;
        LogDigest anything{};
        if (!check(refused(os::boot::verify_inclusion(nullptr, anything, proof, anything),
                           os::boot::transparency_errors::no_hash_provider),
                   "verification proceeded with no hash provider")) return 1;
    }

    return 0;
}
