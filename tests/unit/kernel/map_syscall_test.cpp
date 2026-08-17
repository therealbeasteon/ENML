#include <cstdint>
#include <cstdio>

#include <os/core/error.hpp>
#include <os/kernel/address_space_epoch.hpp>
#include <os/kernel/address_space_syscall.hpp>
#include <os/kernel/executable_region.hpp>
#include <os/kernel/kernel.hpp>
#include <os/kernel/map_syscall.hpp>
#include <os/kernel/memory_grant.hpp>

// The `map` call: its decoder and its authority half.
//
// docs/M7_16_MAP.md is the reasoning. Two properties get the most direct tests
// here because they are the decisions the milestone actually made:
//
//   1. The length is the grant's, never the caller's. There is no register to
//      put one in, so the test that matters is that the authorization's length
//      is the grant's length - including when the caller would plainly have
//      preferred a different one.
//   2. The required right on the space is address_space_right_hold, and holding
//      without it - or holding the *space* but not the *backing* - is refused.

namespace {

bool check(bool condition, const char* what) {
    if (!condition) std::fprintf(stderr, "map syscall: %s\n", what);
    return condition;
}

template <typename T>
bool refused(const os::core::Result<T>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::kernel &&
           result.error().code == code;
}

constexpr os::kernel::ThreadId owner = 21U;
constexpr os::kernel::ThreadId stranger = 22U;
constexpr os::kernel::Priority priority = 4U;

// Distinctive rather than small, for the reason the ABI test gives: a
// transposition of two adjacent arguments is what this file exists to catch,
// and 1/2/3 make one look like another.
constexpr std::uint64_t marker_virtual = 0x0000'0000'4400'0000ULL;
constexpr std::uint64_t marker_physical = 0x0000'0000'8800'0000ULL;
constexpr std::uint64_t marker_length = 0x4000ULL;

} // namespace

int main() {
    using namespace os::kernel;

    // --- The decoder refuses what is wrong about the encoding, and nothing else.
    {
        // Zero names nothing, on either capability.
        if (!check(refused(decode_map_syscall(0ULL, marker_virtual, 7ULL, 2ULL),
                           map_syscall_errors::invalid_capability),
                   "a zero space capability was accepted")) return 1;
        if (!check(refused(decode_map_syscall(7ULL, marker_virtual, 0ULL, 2ULL),
                           map_syscall_errors::invalid_capability),
                   "a zero backing capability was accepted")) return 1;

        // A zeroed register set must not describe a valid mapping.
        if (!check(refused(decode_map_syscall(7ULL, 0ULL, 9ULL, 2ULL),
                           map_syscall_errors::invalid_address),
                   "a zero virtual address was accepted")) return 1;

        // Out of range is refused rather than masked. 6 would become 2 -
        // read_write - under a two-bit mask, which is the most dangerous of the
        // three to be handed by accident.
        for (const std::uint64_t bad : {0ULL, 4ULL, 6ULL, 0xFFFF'FFFF'FFFF'FFFFULL}) {
            if (!check(refused(decode_map_syscall(7ULL, marker_virtual, 9ULL, bad),
                               map_syscall_errors::invalid_permissions),
                       "a permission outside the three was accepted")) return 1;
        }

        // And it accepts all three, with the arguments in the registers the ABI
        // says rather than in some other order that happens to round-trip.
        auto decoded = decode_map_syscall(
            7ULL, marker_virtual, 9ULL,
            static_cast<std::uint64_t>(MapPermissions::read_execute));
        if (!check(static_cast<bool>(decoded), "a well-formed map was refused")) return 1;
        if (!check(decoded.value().space == 7ULL, "x0 is not the space")) return 1;
        if (!check(decoded.value().virtual_address == marker_virtual,
                   "x1 is not the virtual address")) return 1;
        if (!check(decoded.value().backing == 9ULL, "x2 is not the backing")) return 1;
        if (!check(decoded.value().permissions == MapPermissions::read_execute,
                   "x3 is not the permissions")) return 1;

        // Alignment is deliberately NOT refused here - it belongs to the machine
        // layer, which owns the granule and the address layout. Asserted rather
        // than left as an absence, so that adding the check here later is a
        // decision someone makes against a failing test instead of a tidy-up.
        auto unaligned = decode_map_syscall(7ULL, marker_virtual + 1ULL, 9ULL, 1ULL);
        if (!check(static_cast<bool>(unaligned),
                   "the decoder refused an unaligned address, which is the "
                   "machine layer's to refuse - see docs/M7_16_MAP.md")) return 1;
    }

    Kernel kernel{};
    AddressSpaceEpochAuthority epochs{};
    MemoryGrantAuthority grants{};
    ExecutableRegionTable executables{};

    if (!check(static_cast<bool>(kernel.create_thread(owner, priority)),
               "owner thread refused")) return 1;
    if (!check(static_cast<bool>(kernel.create_thread(stranger, priority)),
               "stranger thread refused")) return 1;

    auto authority = kernel.capabilities().mint(
        owner, address_space_authority_object, address_space_right_create, false);
    if (!check(static_cast<bool>(authority), "authority mint refused")) return 1;
    auto space = kernel.address_space_create(owner, authority.value(), epochs);
    if (!check(static_cast<bool>(space), "create refused")) return 1;

    auto grant = grants.create(marker_physical, marker_length);
    if (!check(static_cast<bool>(grant), "grant create refused")) return 1;
    auto backing = kernel.capabilities().mint(
        owner, memory_grant_object_id(grant.value().identity), memory_right_map, true);
    if (!check(static_cast<bool>(backing), "backing mint refused")) return 1;

    const MapSyscall request{
        .space = space.value().capability,
        .virtual_address = marker_virtual,
        .backing = backing.value(),
        .permissions = MapPermissions::read_write,
    };

    // --- The real thing, and the decision it embodies.
    {
        auto authorized = kernel.map_authorize(owner, request, epochs, grants, executables);
        if (!check(static_cast<bool>(authorized), "map_authorize refused a valid map")) return 1;
        if (!check(authorized.value().valid(),
                   "map_authorize returned an incomplete authorization")) return 1;
        if (!check(authorized.value().space == space.value().epoch.identity(),
                   "the authorization names a different space")) return 1;
        if (!check(authorized.value().virtual_base == marker_virtual,
                   "the authorization moved the virtual address")) return 1;
        // The machine layer must not re-resolve, so the resolved physical base
        // has to be here rather than the capability that named it.
        if (!check(authorized.value().physical_base == marker_physical,
                   "the authorization does not carry the resolved physical base")) return 1;
        // The decision: length comes from the grant. Nothing the caller passed
        // could have said otherwise, and this is what says so.
        if (!check(authorized.value().length == marker_length,
                   "the authorization's length is not the grant's")) return 1;
        if (!check(authorized.value().permissions == MapPermissions::read_write,
                   "the authorization changed the permissions")) return 1;
    }

    // --- Authority, on both capabilities independently.
    {
        // Holding neither.
        if (!check(refused(kernel.map_authorize(stranger, request, epochs, grants, executables),
                           address_space_syscall_errors::invalid_capability),
                   "a non-holder mapped into a space")) return 1;

        // Holding the space without the right. hold is what map requires; a
        // capability carrying only destroy is real and says nothing about
        // furnishing the space.
        auto destroy_only = kernel.capabilities().mint(
            owner, address_space_object_id(space.value().epoch.identity()),
            address_space_right_destroy, false);
        if (!check(static_cast<bool>(destroy_only), "destroy-only mint refused")) return 1;
        MapSyscall wrong_right = request;
        wrong_right.space = destroy_only.value();
        if (!check(refused(kernel.map_authorize(owner, wrong_right, epochs, grants, executables),
                           address_space_syscall_errors::invalid_capability),
                   "a capability without hold mapped into a space")) return 1;

        // Holding the space and a backing capability that does not carry
        // memory_right_map. donate is a different authority: spending memory on
        // a kernel object is not the same as mapping it for someone.
        auto donate_only = kernel.capabilities().mint(
            owner, memory_grant_object_id(grant.value().identity),
            memory_right_donate, false);
        if (!check(static_cast<bool>(donate_only), "donate-only mint refused")) return 1;
        MapSyscall wrong_backing = request;
        wrong_backing.backing = donate_only.value();
        if (!check(!kernel.map_authorize(owner, wrong_backing, epochs, grants, executables),
                   "a backing capability without the map right was accepted")) return 1;
    }

    // --- A range that would leave the address space is refused, not wrapped.
    {
        MapSyscall too_high = request;
        too_high.virtual_address = UINT64_MAX - (marker_length / 2ULL);
        if (!check(refused(kernel.map_authorize(owner, too_high, epochs, grants, executables),
                           map_syscall_errors::range_overflows),
                   "a mapping running off the end of the address space was "
                   "accepted, and would have wrapped to a low address")) return 1;
    }

    // --- A retired space is refused, and the capability alone is not enough.
    {
        auto retiring = kernel.address_space_begin_destroy(
            owner, space.value().capability, epochs);
        if (!check(static_cast<bool>(retiring), "begin_destroy refused")) return 1;
        // Still holding the same capability, and the space is on its way out.
        // Mapping into it now would write translations nothing will tear down.
        if (!check(!kernel.map_authorize(owner, request, epochs, grants, executables),
                   "a mapping was authorized into a retiring space")) return 1;
    }

    // --- At most one executable region per space, which is what makes an entry
    // point unnecessary rather than merely unauthorised.
    //
    // docs/M7_16_ENTRY_FROM_REGION.md: a thread admitted into a space begins at
    // the base of that space's executable region, so the region has to be
    // unique or "where does this space begin" has more than one answer. This is
    // the check that keeps it unique, and the table below is what remembers it.
    {
        Kernel k{};
        AddressSpaceEpochAuthority e{};
        MemoryGrantAuthority g{};
        ExecutableRegionTable x{};
        if (!check(static_cast<bool>(k.create_thread(owner, priority)),
                   "second-fixture owner refused")) return 1;
        auto auth = k.capabilities().mint(
            owner, address_space_authority_object, address_space_right_create, false);
        auto sp = k.address_space_create(owner, auth.value(), e);
        if (!check(static_cast<bool>(sp), "second-fixture create refused")) return 1;
        auto gr = g.create(marker_physical, marker_length);
        auto back = k.capabilities().mint(
            owner, memory_grant_object_id(gr.value().identity), memory_right_map, true);

        MapSyscall text{
            .space = sp.value().capability,
            .virtual_address = marker_virtual,
            .backing = back.value(),
            .permissions = MapPermissions::read_execute,
        };

        // The first executable mapping is fine, and recording it is what the
        // dispatch does after the machine layer succeeds.
        auto first = k.map_authorize(owner, text, e, g, x);
        if (!check(static_cast<bool>(first),
                   "the first executable mapping was refused")) return 1;
        if (!check(static_cast<bool>(x.record(
                       sp.value().epoch.identity(),
                       first.value().virtual_base, first.value().length)),
                   "recording the first executable region was refused")) return 1;

        // A second one is refused - at a *different* address, so the refusal is
        // about the space already being executable and not about the mapping
        // already existing, which is a different check in a different layer.
        MapSyscall second_text = text;
        second_text.virtual_address = marker_virtual + 0x10000ULL;
        if (!check(refused(k.map_authorize(owner, second_text, e, g, x),
                           executable_region_errors::already_executable),
                   "a space was given a second executable region")) return 1;

        // Non-executable mappings are unaffected. The rule is about where
        // execution may begin, not about how much memory a space may have.
        MapSyscall data = second_text;
        data.permissions = MapPermissions::read_write;
        if (!check(static_cast<bool>(k.map_authorize(owner, data, e, g, x)),
                   "a data mapping was refused because the space had text")) return 1;

        // And the entry a thread would get is the base of that region - derived,
        // never named.
        auto region = x.region_for(sp.value().epoch.identity());
        if (!check(static_cast<bool>(region), "the recorded region is unreachable")) return 1;
        if (!check(region.value().base == marker_virtual,
                   "the entry is not the base of the executable region")) return 1;

        // A space with nothing executable has nowhere to begin, and says so
        // distinctly rather than looking like a missing capability.
        auto other = k.address_space_create(owner, auth.value(), e);
        if (!check(refused(x.region_for(other.value().epoch.identity()),
                           executable_region_errors::not_executable),
                   "a space with no text did not report it distinctly")) return 1;

        // Teardown releases the slot, and releasing a space that never had a
        // region is not an error.
        if (!check(static_cast<bool>(x.forget(sp.value().epoch.identity())),
                   "forget refused a live region")) return 1;
        if (!check(x.live_count() == 0U, "forget did not release the slot")) return 1;
        if (!check(static_cast<bool>(x.forget(other.value().epoch.identity())),
                   "forgetting a space with no region was an error")) return 1;
    }

    std::fprintf(stderr, "map syscall: ok\n");
    return 0;
}
