#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <os/core/error.hpp>
#include <os/kernel/address_space_syscall.hpp>
#include <os/kernel/interrupt.hpp>
#include <os/kernel/memory_grant.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) std::fprintf(stderr, "memory grant: %s\n", what);
    return condition;
}

template <typename T>
bool refused(const os::core::Result<T>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::kernel &&
           result.error().code == code;
}

constexpr std::uint64_t page = 4096ULL;

} // namespace

// Authority over physical memory: the referent the no-allocator decision was
// missing. The properties worth testing are the ones that stop a grant from
// meaning less than it looks like it means - that two holders cannot be told
// they own the same memory, that a revoked name does not reach its successor,
// and that containment is not fooled by a range that wraps.
int main() {
    using namespace os::kernel;

    MemoryGrantAuthority grants{};

    // Containment is the whole point of holding a grant, so it is checked
    // before anything else: a grant answers "may this holder spend this
    // range", and getting that wrong in either direction is the bug.
    auto first = grants.create(0x4000'0000ULL, 4ULL * page);
    if (!check(static_cast<bool>(first), "create refused")) return 1;
    if (!check(first.value().valid(), "created grant is not valid")) return 1;
    if (!check(grants.live_count() == 1U, "wrong live count")) return 1;

    if (!check(first.value().contains(0x4000'0000ULL, page), "start page not contained")) return 1;
    if (!check(first.value().contains(0x4000'3000ULL, page), "last page not contained")) return 1;
    if (!check(first.value().contains(0x4000'0000ULL, 4ULL * page),
               "the whole range is not contained in itself")) return 1;
    if (!check(!first.value().contains(0x4000'4000ULL, page),
               "a page past the end was contained")) return 1;
    if (!check(!first.value().contains(0x3FFF'F000ULL, page),
               "a page before the start was contained")) return 1;
    if (!check(!first.value().contains(0x4000'3000ULL, 2ULL * page),
               "a range overrunning the end was contained")) return 1;
    if (!check(!first.value().contains(0x4000'0000ULL, 0ULL),
               "an empty range was contained")) return 1;

    // A range that wraps must not be satisfied by comparing wrapped values.
    // This is the case that would let a holder of a small grant claim
    // authority over everything by naming a length that overflows past it.
    if (!check(!first.value().contains(0x4000'0000ULL, UINT64_MAX),
               "a wrapping range was contained")) return 1;

    // Overlap is refused at creation, where the mistake is. Two live grants
    // over one range would let two holders each believe they may spend it.
    if (!check(refused(grants.create(0x4000'1000ULL, page),
                       memory_grant_errors::overlapping),
               "an overlapping grant was created")) return 1;
    if (!check(refused(grants.create(0x3FFF'F000ULL, 2ULL * page),
                       memory_grant_errors::overlapping),
               "a grant overlapping the start was created")) return 1;
    if (!check(grants.live_count() == 1U,
               "a refused create changed the live count")) return 1;

    // Adjacent is not overlapping, or memory could never be handed out in
    // pieces that meet.
    auto neighbour = grants.create(0x4000'4000ULL, page);
    if (!check(static_cast<bool>(neighbour), "an adjacent grant was refused")) return 1;

    // A wrapping or empty grant cannot be created at all.
    if (!check(refused(grants.create(0xFFFF'FFFF'FFFF'F000ULL, 2ULL * page),
                       memory_grant_errors::invalid_range),
               "a wrapping grant was created")) return 1;
    if (!check(refused(grants.create(0x5000'0000ULL, 0ULL),
                       memory_grant_errors::invalid_range),
               "an empty grant was created")) return 1;

    // Resolve returns what was granted, and a revoked identity stops
    // resolving - with `stale`, the same answer an identity that never
    // existed gets, so a holder cannot tell the two apart.
    {
        auto resolved = grants.resolve(first.value().identity);
        if (!check(static_cast<bool>(resolved), "a live grant did not resolve")) return 1;
        if (!check(resolved.value().physical_base == 0x4000'0000ULL &&
                   resolved.value().length == 4ULL * page,
                   "resolve returned a different range")) return 1;
    }
    if (!check(refused(grants.resolve(MemoryGrantIdentity{0U, 999U}),
                       memory_grant_errors::stale),
               "an identity that never existed resolved")) return 1;

    const auto revoked_identity = first.value().identity;
    if (!check(static_cast<bool>(grants.revoke(revoked_identity)),
               "revoke refused")) return 1;
    if (!check(refused(grants.resolve(revoked_identity), memory_grant_errors::stale),
               "a revoked grant still resolved")) return 1;
    if (!check(refused(grants.revoke(revoked_identity), memory_grant_errors::stale),
               "a grant was revoked twice")) return 1;
    if (!check(grants.live_count() == 1U, "revoke did not free the slot")) return 1;

    // The exit-criterion shape, same as address spaces: the freed slot is
    // reissued with a new generation, and the old identity does not name it.
    auto reissued = grants.create(0x4000'0000ULL, page);
    if (!check(static_cast<bool>(reissued), "the freed range could not be regranted")) return 1;
    if (!check(reissued.value().identity.slot == revoked_identity.slot,
               "the freed slot was not reused")) return 1;
    if (!check(reissued.value().identity.generation != revoked_identity.generation,
               "a reissued slot kept its generation")) return 1;
    if (!check(refused(grants.resolve(revoked_identity), memory_grant_errors::stale),
               "a stale identity reached the grant that replaced it")) return 1;

    // The object id carries the generation, so a capability minted over the
    // revoked grant does not name its successor either.
    if (!check(memory_grant_object_id(revoked_identity) !=
               memory_grant_object_id(reissued.value().identity),
               "a reissued grant shares the old object id")) return 1;
    if (!check((memory_grant_object_id(reissued.value().identity) &
                memory_grant_object_tag_mask) == memory_grant_object_tag,
               "the tag did not survive encoding")) return 1;
    if (!check(memory_grant_object_id(MemoryGrantIdentity{}) == invalid_object,
               "an invalid identity encoded to something")) return 1;

    // Namespaces must not collide: a capability over memory grant 3 must not
    // be spendable as one over address space 3 or interrupt source 3.
    static_assert(memory_grant_object_tag != address_space_object_tag);
    static_assert(memory_grant_object_tag != interrupt_object_tag);

    // Two rights, independently expressible. Donating memory to become a
    // translation table is not the same authority as mapping it.
    static_assert(memory_right_map != memory_right_donate);
    static_assert((memory_right_map & memory_right_donate) == 0U);

    return 0;
}
