#include <cstdint>
#include <cstdio>

#include <os/core/error.hpp>
#include <os/kernel/capability.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) std::fprintf(stderr, "context-bound capability: %s\n", what);
    return condition;
}

template <typename T>
bool refused(const os::core::Result<T>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::kernel &&
           result.error().code == code;
}

constexpr os::kernel::ThreadId thread = 17U;
constexpr os::kernel::ThreadId peer_thread = 23U;
constexpr os::kernel::ObjectId object = 0xC001U;
constexpr os::kernel::Rights rights = 0b11U;

constexpr os::kernel::ExecutionAuthority first{
    thread,
    os::kernel::AddressSpaceIdentity{4U, 12U}};
constexpr os::kernel::ExecutionAuthority recycled{
    thread,
    os::kernel::AddressSpaceIdentity{4U, 13U}};
constexpr os::kernel::ExecutionAuthority peer{
    peer_thread,
    os::kernel::AddressSpaceIdentity{5U, 7U}};

} // namespace

int main() {
    using namespace os::kernel;

    static_assert(first.valid());
    static_assert(recycled.valid());
    static_assert(peer.valid());
    static_assert(first.thread == recycled.thread);
    static_assert(first.address_space != recycled.address_space);

    CapabilityTable table{};

    auto root = table.mint(first, object, rights, true);
    if (!check(static_cast<bool>(root), "context-bound mint refused")) return 1;

    auto info = table.describe(root.value());
    if (!check(static_cast<bool>(info), "describe refused")) return 1;
    if (!check(info.value().context_bound, "context-bound flag missing")) return 1;
    if (!check(info.value().holder_authority() == first, "stored authority changed")) return 1;

    if (!check(table.holds(first, root.value()), "original authority lost capability")) return 1;
    if (!check(!table.holds(recycled, root.value()), "new generation inherited stale capability")) return 1;
    if (!check(!table.holds(thread, root.value()), "legacy ThreadId bypassed context binding")) return 1;

    if (!check(refused(table.grant(recycled, root.value(), peer, rights, false),
                       capability_errors::not_holder),
               "recycled generation granted from stale capability")) return 1;
    if (!check(refused(table.grant(thread, root.value(), peer_thread, rights, false),
                       capability_errors::not_holder),
               "legacy grant bypassed context binding")) return 1;

    auto child = table.grant(first, root.value(), peer, 0b01U, false);
    if (!check(static_cast<bool>(child), "valid context-bound grant refused")) return 1;
    auto child_info = table.describe(child.value());
    if (!check(static_cast<bool>(child_info), "child describe refused")) return 1;
    if (!check(child_info.value().holder_authority() == peer,
               "child did not bind recipient generation")) return 1;

    if (!check(refused(table.revoke(recycled, root.value()),
                       capability_errors::not_revocable),
               "recycled generation revoked stale capability")) return 1;
    if (!check(refused(table.revoke(thread, root.value()),
                       capability_errors::not_revocable),
               "legacy revoke bypassed context binding")) return 1;

    auto removed = table.revoke(first, root.value());
    if (!check(static_cast<bool>(removed) && removed.value() == 2U,
               "valid holder did not revoke derivation subtree")) return 1;
    if (!check(table.live_capability_count() == 0U,
               "revocation left context-bound authority behind")) return 1;

    // The migration boundary is bidirectional. A stronger ExecutionAuthority
    // must not be accepted as authority over a legacy ThreadId-only capability;
    // otherwise callers could silently cross protection modes while M7.8 is
    // being introduced.
    auto legacy = table.mint(thread, object, rights, true);
    if (!check(static_cast<bool>(legacy), "legacy setup mint refused")) return 1;
    if (!check(table.holds(thread, legacy.value()), "legacy holder lost legacy capability")) return 1;
    if (!check(!table.holds(first, legacy.value()),
               "context authority exercised a legacy capability")) return 1;
    if (!check(refused(table.grant(first, legacy.value(), peer, rights, false),
                       capability_errors::not_holder),
               "context grant crossed into legacy authority")) return 1;
    if (!check(refused(table.revoke(first, legacy.value()),
                       capability_errors::not_revocable),
               "context revoke crossed into legacy authority")) return 1;
    auto legacy_removed = table.revoke(thread, legacy.value());
    if (!check(static_cast<bool>(legacy_removed) && legacy_removed.value() == 1U,
               "legacy holder could not revoke legacy capability")) return 1;

    // Administrative teardown is intentionally stronger than a process claim:
    // destroying a numeric thread must still remove every generation it holds.
    auto teardown_root = table.mint(first, object, rights, true);
    auto teardown_child = table.grant(first, teardown_root.value(), peer, rights, false);
    if (!check(static_cast<bool>(teardown_child), "teardown setup failed")) return 1;
    if (!check(table.revoke_all_held_by(thread) == 2U,
               "thread teardown did not remove context-bound subtree")) return 1;
    if (!check(table.live_capability_count() == 0U,
               "thread teardown leaked capability authority")) return 1;

    return 0;
}
