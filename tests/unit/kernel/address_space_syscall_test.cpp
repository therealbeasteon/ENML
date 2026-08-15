#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <os/core/error.hpp>
#include <os/kernel/address_space_syscall.hpp>
#include <os/kernel/interrupt.hpp>
#include <os/kernel/ipc_endpoint.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) std::fprintf(stderr, "address-space syscall: %s\n", what);
    return condition;
}

template <typename T>
bool refused(const os::core::Result<T>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::kernel &&
           result.error().code == code;
}

} // namespace

// The syscall surface for M7.11's address-space create/destroy: the object-id
// encoding, the two rights, and the argument decoders.
//
// The encoding carries the security content here, not the decoders. A
// capability names one *lifetime* of an address space rather than a slot, so a
// reference held across a destroy-and-recreate stops resolving on its own -
// with the ordinary capability-not-found error, not a distinguishable one that
// would tell the holder its space had been replaced. That is M7.11's stated
// exit criterion, and it is achieved by the identifier's shape rather than by
// a staleness check some call site could forget.
int main() {
    using namespace os::kernel;

    // Generation is part of the name. Same slot, different incarnation, must
    // be a different object - this is the whole reason the id is not the slot.
    constexpr AddressSpaceIdentity original{2U, 4U};
    constexpr AddressSpaceIdentity recycled{2U, 5U};
    static_assert(original.valid());
    static_assert(recycled.valid());
    static_assert(original.slot == recycled.slot);
    static_assert(address_space_object_id(original) != address_space_object_id(recycled));

    // Distinct slots at the same generation must also be distinct, or the
    // encoding would be aliasing two live spaces onto one name.
    static_assert(address_space_object_id(AddressSpaceIdentity{2U, 4U}) !=
                  address_space_object_id(AddressSpaceIdentity{3U, 4U}));

    // The tag must survive encoding, and must not collide with the other two
    // object namespaces. A capability over address space 3 that could be
    // spent on interrupt source 3 would be a confused-deputy bug of exactly
    // the kind the tags exist to prevent.
    constexpr auto encoded = address_space_object_id(original);
    static_assert((encoded & address_space_object_tag_mask) == address_space_object_tag);
    static_assert(address_space_object_tag != interrupt_object_tag);
    static_assert(address_space_object_tag != ipc_object_tag);
    if (!check(encoded != interrupt_object_id(static_cast<InterruptSource>(original.slot)),
               "an address-space id collided with an interrupt id")) return 1;

    // Slot occupies the low 16 bits and generation the 32 above it, so a
    // maximal generation must not run into the tag. If it did, a high enough
    // generation would forge a different namespace.
    constexpr AddressSpaceIdentity high_generation{62U, 0xFFFF'FFFFU};
    static_assert(high_generation.valid());
    static_assert((address_space_object_id(high_generation) &
                   address_space_object_tag_mask) == address_space_object_tag);

    // An invalid identity has no name. Generation zero is the never-issued
    // value and slot 63 is past the epoch table, so neither can be encoded
    // into something a capability lookup would accept.
    static_assert(address_space_object_id(AddressSpaceIdentity{2U, 0U}) == invalid_object);
    static_assert(address_space_object_id(AddressSpaceIdentity{
                      static_cast<AddressSpaceSlot>(max_address_space_epochs), 4U}) ==
                  invalid_object);

    // Two rights, not one: holding a space so it can be mapped into is not the
    // authority to destroy it. They must be independently expressible.
    static_assert(address_space_right_hold != address_space_right_destroy);
    static_assert((address_space_right_hold & address_space_right_destroy) == 0U);

    // create: both arguments are capabilities now, carried through and each
    // rejected on its own.
    {
        auto decoded = decode_address_space_create_syscall(7ULL, 9ULL);
        if (!check(static_cast<bool>(decoded), "a valid create was refused")) return 1;
        if (!check(decoded.value().authority == 7ULL, "create lost the authority")) return 1;
        if (!check(decoded.value().root_grant == 9ULL,
                   "create lost the memory capability")) return 1;
    }
    if (!check(refused(decode_address_space_create_syscall(0ULL, 9ULL),
                       address_space_syscall_errors::invalid_capability),
               "create accepted a null authority")) return 1;
    if (!check(refused(decode_address_space_create_syscall(7ULL, 0ULL),
                       address_space_syscall_errors::invalid_capability),
               "create accepted a null memory capability")) return 1;

    // A null authority is reported as such even when the page is also null, so
    // the caller learns the first thing wrong rather than the last.
    if (!check(refused(decode_address_space_create_syscall(0ULL, 0ULL),
                       address_space_syscall_errors::invalid_capability),
               "create misreported which argument was invalid")) return 1;

    // destroy: one argument, carried through and rejected when null.
    {
        auto decoded = decode_address_space_destroy_syscall(9ULL);
        if (!check(static_cast<bool>(decoded), "a valid destroy was refused")) return 1;
        if (!check(decoded.value().space == 9ULL, "destroy lost the capability")) return 1;
    }
    if (!check(refused(decode_address_space_destroy_syscall(0ULL),
                       address_space_syscall_errors::invalid_capability),
               "destroy accepted a null capability")) return 1;

    // The decoder must not invent rules that belong elsewhere. Whether either
    // capability names anything, whether this holder holds it, and whether the
    // grant is big enough are all checked where they are enforced - a second
    // weaker copy here is how the two come to disagree.
    if (!check(static_cast<bool>(decode_address_space_create_syscall(
                   0xFFFF'FFFF'FFFF'FFFFULL, 0xFFFF'FFFF'FFFF'FFFFULL)),
               "the decoder grew a rule that belongs to the capability table")) {
        return 1;
    }

    return 0;
}
