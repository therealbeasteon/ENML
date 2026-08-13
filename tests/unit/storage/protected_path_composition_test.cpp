#include <cassert>
#include <string>

#include <os/storage/error.hpp>
#include <os/storage/path.hpp>

int main() {
    auto parent = os::storage::RelativePath::parse("vault/documents");
    auto child = os::storage::RelativePath::parse("notes/state.bin");
    assert(parent && child);

    auto joined = os::storage::join_relative_paths(parent.value(), child.value());
    assert(joined);
    assert(joined.value().view() == "vault/documents/notes/state.bin");

    const std::string near_limit(os::storage::max_relative_path_bytes - 1U, 'a');
    auto oversized_parent = os::storage::RelativePath::parse(near_limit);
    auto leaf = os::storage::RelativePath::parse("b");
    // The parent itself is rejected earlier because one segment exceeds the
    // per-segment ceiling; use many valid segments to exercise aggregate join.
    assert(!oversized_parent);
    assert(leaf);

    // Grow to the largest parent that still parses, so that adding a
    // separator and a one-byte leaf must overflow. The previous bound stopped
    // two bytes early and produced a 1021-byte parent, which joins to 1023 and
    // fits inside the 1024-byte ceiling - so this case asserted an overflow
    // that the arithmetic could never reach.
    std::string segmented;
    while (segmented.size() + 2U <= os::storage::max_relative_path_bytes) {
        if (!segmented.empty()) segmented.push_back('/');
        segmented.push_back('a');
    }
    auto long_parent = os::storage::RelativePath::parse(segmented);
    assert(long_parent);
    auto overflow = os::storage::join_relative_paths(long_parent.value(), leaf.value());
    assert(!overflow);
    assert(overflow.error().code == os::storage::errors::path_too_long);

    return 0;
}
