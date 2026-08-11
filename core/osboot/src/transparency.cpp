#include <os/boot/transparency.hpp>

#include <os/core/error.hpp>
#include <os/core/secret.hpp>

namespace os::boot {
namespace {

[[nodiscard]] constexpr os::core::Error transparency_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::security, code);
}

[[nodiscard]] os::core::ByteSpan bytes_of(const LogDigest& digest) noexcept {
    return os::core::ByteSpan{reinterpret_cast<const std::byte*>(digest.data()), digest.size()};
}

// One internal node from its two children, domain-separated from a leaf.
//
// The prefix is the whole defence. Without it, an attacker presents the
// concatenation of two child digests as if it were a leaf's contents, and the
// resulting proof verifies against a root nobody ever committed that value to.
void hash_node(
    HashFunction hash,
    const LogDigest& left,
    const LogDigest& right,
    LogDigest& out) noexcept {
    std::array<std::uint8_t, 1U + (2U * measurement_digest_bytes)> buffer{};
    buffer[0] = node_prefix;
    for (std::size_t i = 0U; i < measurement_digest_bytes; ++i) {
        buffer[1U + i] = left[i];
        buffer[1U + measurement_digest_bytes + i] = right[i];
    }
    hash(buffer.data(), buffer.size(), out);
}

} // namespace

os::core::Result<std::size_t> expected_path_length(
    std::uint64_t leaf_index,
    std::uint64_t tree_size) noexcept {
    if (tree_size == 0U) {
        return os::core::Result<std::size_t>{
            transparency_error(transparency_errors::invalid_tree)};
    }
    if (leaf_index >= tree_size) {
        return os::core::Result<std::size_t>{
            transparency_error(transparency_errors::invalid_leaf_index)};
    }

    // Walk the tree the way the verifier will, counting the levels at which this
    // leaf actually has a sibling. A subtree with one node contributes no step,
    // which is what makes the length depend on the tree's shape rather than on
    // its depth alone - and is why the caller is not trusted to supply it.
    std::size_t levels = 0U;
    std::uint64_t index = leaf_index;
    std::uint64_t size = tree_size;
    while (size > 1U) {
        if (levels > max_inclusion_path) {
            return os::core::Result<std::size_t>{
                transparency_error(transparency_errors::path_too_long)};
        }
        // Split at the largest power of two below `size`, which is the standard
        // shape for an append-only log: the left subtree is always full.
        std::uint64_t split = 1U;
        while ((split << 1U) < size) split <<= 1U;

        if (index < split) {
            size = split;
        } else {
            index -= split;
            size -= split;
        }
        ++levels;
    }
    if (levels > max_inclusion_path) {
        return os::core::Result<std::size_t>{
            transparency_error(transparency_errors::path_too_long)};
    }
    return os::core::Result<std::size_t>{levels};
}

os::core::Result<LogDigest> hash_leaf(
    HashFunction hash,
    const std::uint8_t* data,
    std::size_t length) noexcept {
    if (hash == nullptr || (data == nullptr && length != 0U)) {
        return os::core::Result<LogDigest>{
            transparency_error(transparency_errors::no_hash_provider)};
    }

    // Prefixed, always. A leaf hashed bare is a leaf an internal node can
    // impersonate.
    std::array<std::uint8_t, 1U> prefix{leaf_prefix};
    // The provider hashes one contiguous buffer, so the prefix is prepended into
    // a bounded staging buffer rather than streamed. Oversized payloads are the
    // caller's to chunk before they reach a log entry.
    if (length > 4096U) {
        return os::core::Result<LogDigest>{
            transparency_error(transparency_errors::invalid_tree)};
    }
    std::array<std::uint8_t, 1U + 4096U> buffer{};
    buffer[0] = prefix[0];
    for (std::size_t i = 0U; i < length; ++i) {
        buffer[1U + i] = data[i];
    }

    LogDigest out{};
    hash(buffer.data(), length + 1U, out);
    return os::core::Result<LogDigest>{out};
}

os::core::Result<void> verify_inclusion(
    HashFunction hash,
    const LogDigest& leaf,
    const InclusionProof& proof,
    const LogDigest& root) noexcept {
    if (hash == nullptr) {
        return transparency_error(transparency_errors::no_hash_provider);
    }
    if (proof.path_length > max_inclusion_path) {
        return transparency_error(transparency_errors::path_too_long);
    }

    auto required = expected_path_length(proof.leaf_index, proof.tree_size);
    if (!required) {
        return required.error();
    }
    // The supplied length must be the one the tree size implies. A verifier that
    // consumed whatever it was handed would let an attacker choose the shape of
    // the proof, and the collision study is explicit that path length is where
    // the probability of a colliding root lives.
    if (required.value() != proof.path_length) {
        return transparency_error(transparency_errors::path_length_mismatch);
    }

    // Record which side the leaf falls on at each level, walking down, then
    // combine walking back up.
    //
    // The audit path is ordered bottom-up - the leaf's own sibling first - which
    // is the conventional ordering and the one a log implementation will emit.
    // The descent that identifies the siblings necessarily runs top-down, so the
    // two have to be reconciled explicitly. Doing the combination during the
    // descent instead is the obvious shortcut and is simply wrong: it folds the
    // leaf against the *topmost* sibling first, which produces a root only when
    // the tree is one level deep. An exhaustive test over every leaf of every
    // log size up to thirty-three is what caught it.
    std::array<bool, max_inclusion_path> leaf_is_left{};
    std::size_t levels = 0U;
    std::uint64_t index = proof.leaf_index;
    std::uint64_t size = proof.tree_size;

    while (size > 1U) {
        std::uint64_t split = 1U;
        while ((split << 1U) < size) split <<= 1U;

        if (index < split) {
            leaf_is_left[levels] = true;
            size = split;
        } else {
            leaf_is_left[levels] = false;
            index -= split;
            size -= split;
        }
        ++levels;
    }

    LogDigest computed = leaf;
    for (std::size_t i = 0U; i < levels; ++i) {
        const std::size_t level = levels - 1U - i;
        LogDigest combined{};
        if (leaf_is_left[level]) {
            hash_node(hash, computed, proof.path[i], combined);
        } else {
            hash_node(hash, proof.path[i], computed, combined);
        }
        computed = combined;
    }

    // Constant-time, for the same reason the sealing comparison is: how much of
    // a root a forger has matched is information, and a compare that stops at
    // the first difference hands the rest over a byte at a time.
    if (!os::core::constant_time_equal(bytes_of(computed), bytes_of(root))) {
        return transparency_error(transparency_errors::root_mismatch);
    }
    return {};
}

} // namespace os::boot
