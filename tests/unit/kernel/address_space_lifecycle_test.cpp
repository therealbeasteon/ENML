#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <os/core/error.hpp>
#include <os/kernel/address_space_epoch.hpp>
#include <os/kernel/address_space_syscall.hpp>
#include <os/kernel/kernel.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) std::fprintf(stderr, "address-space lifecycle: %s\n", what);
    return condition;
}

template <typename T>
bool refused(const os::core::Result<T>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::kernel &&
           result.error().code == code;
}

constexpr os::kernel::ThreadId creator = 11U;
constexpr os::kernel::ThreadId stranger = 12U;
constexpr os::kernel::Priority priority = 4U;

} // namespace

// M7.11's Kernel composition: who may create an address space, who may destroy
// one, and what a capability over a destroyed one is worth.
//
// The last is the milestone's stated exit criterion and the reason the object
// id carries a generation, so it gets the most direct test here: a capability
// minted over a space that has since been destroyed must not reach the *next*
// space to occupy that slot, and must fail the way any unknown reference does.
int main() {
    using namespace os::kernel;

    Kernel kernel{};
    AddressSpaceEpochAuthority epochs{};
    if (!check(static_cast<bool>(kernel.create_thread(creator, priority)),
               "creator thread refused")) return 1;
    if (!check(static_cast<bool>(kernel.create_thread(stranger, priority)),
               "stranger thread refused")) return 1;

    auto authority = kernel.capabilities().mint(
        creator, address_space_authority_object, address_space_right_create, false);
    if (!check(static_cast<bool>(authority), "authority mint refused")) return 1;

    // Creation is gated. A thread with no capability at all cannot create, and
    // neither can one holding a capability that is real but says nothing about
    // creating address spaces.
    if (!check(refused(kernel.address_space_create(stranger, authority.value(), epochs),
                       address_space_syscall_errors::invalid_capability),
               "a non-holder created an address space")) return 1;
    {
        auto wrong_right = kernel.capabilities().mint(
            creator, address_space_authority_object, address_space_right_hold, false);
        if (!check(static_cast<bool>(wrong_right), "hold-only mint refused")) return 1;
        if (!check(refused(kernel.address_space_create(creator, wrong_right.value(), epochs),
                           address_space_syscall_errors::invalid_capability),
                   "a capability without the create right created a space")) return 1;
    }

    // The real thing.
    auto first = kernel.address_space_create(creator, authority.value(), epochs);
    if (!check(static_cast<bool>(first), "create refused")) return 1;
    if (!check(first.value().valid(), "create returned an incomplete result")) return 1;
    if (!check(epochs.active(first.value().epoch), "created epoch is not active")) return 1;
    if (!check(epochs.active_count() == 1U, "wrong active count after create")) return 1;

    const auto first_identity = first.value().epoch.identity();
    {
        auto described = kernel.capabilities().describe(first.value().capability);
        if (!check(static_cast<bool>(described), "created capability not describable")) return 1;
        if (!check(described.value().object == address_space_object_id(first_identity),
                   "the capability does not name the epoch it was created with")) return 1;
        if (!check((described.value().rights & address_space_right_destroy) != 0U,
                   "the creator did not receive the destroy right")) return 1;
    }

    // The authority capability carries the address-space tag but names no
    // space - its generation is zero. It must not be usable to destroy one.
    if (!check(refused(kernel.address_space_begin_destroy(creator, authority.value(), epochs),
                       address_space_syscall_errors::invalid_capability),
               "the creation authority was accepted as a space to destroy")) return 1;

    // A second space, so the slot the first one occupies is not the only one
    // in play when the recycling check runs below.
    auto second = kernel.address_space_create(creator, authority.value(), epochs);
    if (!check(static_cast<bool>(second), "second create refused")) return 1;
    if (!check(second.value().epoch.identity() != first_identity,
               "two live spaces share an identity")) return 1;

    // Destroy the first, in the two phases the ASID lifecycle requires.
    auto retiring = kernel.address_space_begin_destroy(
        creator, first.value().capability, epochs);
    if (!check(static_cast<bool>(retiring), "begin_destroy refused")) return 1;
    if (!check(epochs.retiring(retiring.value()), "epoch is not retiring after begin")) return 1;
    if (!check(!epochs.active(first.value().epoch),
               "a retiring epoch still reports active")) return 1;

    // Completing with a capability that names a different space must be
    // refused: it would release an ASID whose translations the machine layer
    // may not have invalidated.
    if (!check(refused(kernel.address_space_complete_destroy(
                           creator, second.value().capability, retiring.value(), epochs),
                       address_space_syscall_errors::invalid_capability),
               "completion accepted a capability over a different space")) return 1;

    if (!check(static_cast<bool>(kernel.address_space_complete_destroy(
                   creator, first.value().capability, retiring.value(), epochs)),
               "complete_destroy refused")) return 1;

    // The capability naming the destroyed space is gone with it.
    if (!check(!kernel.capabilities().holds(creator, first.value().capability),
               "the destroy capability outlived the space it named")) return 1;

    // The exit criterion. Recreate until the freed slot comes back, then prove
    // a capability minted over the *old* identity does not reach the space now
    // occupying that slot - and is refused as stale, the same answer any
    // identity that names nothing gets, rather than one that would tell the
    // holder its space had been replaced.
    AddressSpaceCreation recycled{};
    for (std::size_t attempt = 0U; attempt < max_address_space_epochs; ++attempt) {
        auto again = kernel.address_space_create(creator, authority.value(), epochs);
        if (!check(static_cast<bool>(again), "recreate refused")) return 1;
        if (again.value().epoch.slot == first_identity.slot) {
            recycled = again.value();
            break;
        }
    }
    if (!check(recycled.valid(), "the freed slot was never reissued")) return 1;
    if (!check(recycled.epoch.generation != first_identity.generation,
               "a reissued slot kept its old generation")) return 1;

    auto stale_capability = kernel.capabilities().mint(
        creator, address_space_object_id(first_identity),
        address_space_right_hold | address_space_right_destroy, false);
    if (!check(static_cast<bool>(stale_capability), "stale mint refused")) return 1;
    if (!check(refused(kernel.address_space_begin_destroy(
                           creator, stale_capability.value(), epochs),
                       address_space_epoch_errors::stale),
               "a capability over a destroyed space reached its successor")) return 1;
    if (!check(epochs.active(recycled.epoch),
               "the successor was disturbed by a stale capability")) return 1;

    // resolve() is the primitive that property rests on, so check it directly:
    // the old identity names nothing, the new one names the live epoch.
    if (!check(refused(epochs.resolve(first_identity), address_space_epoch_errors::stale),
               "a destroyed identity still resolved")) return 1;
    {
        auto resolved = epochs.resolve(recycled.epoch.identity());
        if (!check(static_cast<bool>(resolved), "a live identity did not resolve")) return 1;
        if (!check(resolved.value() == recycled.epoch,
                   "resolve returned a different epoch than the one created")) return 1;
    }

    return 0;
}
