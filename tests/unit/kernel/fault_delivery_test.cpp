#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <os/core/error.hpp>
#include <os/kernel/fault_delivery.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) std::fprintf(stderr, "fault delivery: %s\n", what);
    return condition;
}

template <typename T>
bool refused(const os::core::Result<T>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::kernel &&
           result.error().code == code;
}

constexpr os::kernel::ThreadId pager = 7U;
constexpr os::kernel::ThreadId other_pager = 8U;
constexpr os::kernel::ThreadId faulting = 9U;

constexpr os::kernel::FaultReport deliverable{
    3U, os::kernel::FaultDisposition::deliver, true};

} // namespace

// The question a fault asks, held between the kernel asking it and the pager
// answering. The properties worth testing are the ones that stop a pager
// gaining something the disclosure decision withholds, and the ones that stop
// a faulting thread being forgotten.
int main() {
    using namespace os::kernel;

    FaultDeliveryTable deliveries{};
    if (!check(!deliveries.armed(pager), "a fresh table had something armed")) return 1;

    // Only a deliverable report may be asked about. A terminate disposition is
    // what `sealed` regions and undeclared memory produce, and arming one
    // would route around the disclosure decision rather than enforce it.
    constexpr FaultReport terminate{3U, FaultDisposition::terminate, false};
    if (!check(refused(deliveries.arm(pager, faulting, terminate),
                       fault_delivery_errors::not_armed),
               "a terminate report was delivered to a pager")) return 1;
    constexpr FaultReport no_region{
        invalid_fault_region, FaultDisposition::deliver, false};
    if (!check(refused(deliveries.arm(pager, faulting, no_region),
                       fault_delivery_errors::not_armed),
               "a report naming no region was delivered")) return 1;

    // A pager cannot be asked about its own fault - it would be blocked from
    // answering the question it is holding.
    if (!check(refused(deliveries.arm(pager, pager, deliverable),
                       fault_delivery_errors::invalid_thread),
               "a pager was asked about its own fault")) return 1;
    if (!check(deliveries.outstanding() == 0U,
               "a refused arm occupied a slot")) return 1;

    // The real thing.
    if (!check(static_cast<bool>(deliveries.arm(pager, faulting, deliverable)),
               "arm refused")) return 1;
    if (!check(deliveries.armed(pager), "armed did not report the question")) return 1;
    if (!check(deliveries.outstanding() == 1U, "wrong outstanding count")) return 1;

    // Refuse rather than overwrite. Overwriting would strand the first
    // faulting thread waiting on an answer nobody now owes it.
    if (!check(refused(deliveries.arm(pager, 10U, deliverable),
                       fault_delivery_errors::already_armed),
               "a second question overwrote the first")) return 1;

    // Answering before collecting is not an answer.
    if (!check(refused(deliveries.answer(pager), fault_delivery_errors::not_delivered),
               "a pager answered a question it had not been told")) return 1;

    // Collecting leaves the slot occupied, because the faulting thread still
    // has to be found when the answer arrives. This is the whole reason the
    // table has two states rather than one.
    {
        auto taken = deliveries.take(pager);
        if (!check(static_cast<bool>(taken), "take refused")) return 1;
        if (!check(taken.value().region == deliverable.region &&
                   taken.value().faulting == faulting &&
                   taken.value().write,
                   "take returned a different question")) return 1;
    }
    if (!check(deliveries.armed(pager), "the slot was freed by take")) return 1;
    if (!check(deliveries.outstanding() == 1U, "take dropped the outstanding count")) return 1;

    // Taking twice is refused: the question has been asked and is now owed.
    if (!check(refused(deliveries.take(pager), fault_delivery_errors::not_armed),
               "the same question was collected twice")) return 1;

    // The answer ends it, and returns who was waiting.
    {
        auto answered = deliveries.answer(pager);
        if (!check(static_cast<bool>(answered), "answer refused")) return 1;
        if (!check(answered.value().faulting == faulting,
                   "answer lost the faulting thread")) return 1;
        if (!check(answered.value().region == deliverable.region,
                   "answer lost the region")) return 1;
    }
    if (!check(!deliveries.armed(pager), "the slot survived the answer")) return 1;
    if (!check(deliveries.outstanding() == 0U, "answer did not free the slot")) return 1;
    if (!check(refused(deliveries.answer(pager), fault_delivery_errors::not_armed),
               "a pager answered twice")) return 1;

    // A pager that dies owing an answer hands back the thread waiting on it,
    // in both states. Either way that thread is blocked on an answer that is
    // never coming, and the caller has to know which one to terminate.
    if (!check(static_cast<bool>(deliveries.arm(pager, faulting, deliverable)),
               "re-arm refused")) return 1;
    {
        const auto released = deliveries.release(pager);
        if (!check(released.valid(), "release of a pending question returned nothing")) return 1;
        if (!check(released.faulting == faulting, "release lost the faulting thread")) return 1;
    }
    if (!check(deliveries.outstanding() == 0U, "release did not free the slot")) return 1;

    if (!check(static_cast<bool>(deliveries.arm(pager, faulting, deliverable)),
               "re-arm after release refused")) return 1;
    if (!check(static_cast<bool>(deliveries.take(pager)), "take after re-arm refused")) return 1;
    {
        const auto released = deliveries.release(pager);
        if (!check(released.valid(),
                   "release of a delivered question returned nothing")) return 1;
        if (!check(released.faulting == faulting,
                   "release after take lost the faulting thread")) return 1;
    }
    if (!check(!deliveries.release(pager).valid(),
               "releasing a pager that owed nothing returned a delivery")) return 1;

    // Two pagers are independent: one owing an answer must not affect another.
    if (!check(static_cast<bool>(deliveries.arm(pager, faulting, deliverable)),
               "first pager arm refused")) return 1;
    if (!check(static_cast<bool>(deliveries.arm(other_pager, 11U, deliverable)),
               "second pager was refused because the first was armed")) return 1;
    if (!check(deliveries.outstanding() == 2U, "wrong count with two pagers")) return 1;
    {
        auto taken = deliveries.take(other_pager);
        if (!check(static_cast<bool>(taken), "second pager could not collect")) return 1;
        if (!check(taken.value().faulting == 11U,
                   "the wrong question was handed to the second pager")) return 1;
    }
    if (!check(deliveries.armed(pager),
               "collecting one pager's question disturbed another's")) return 1;

    // The answer half of the handshake. One argument, and it names backing
    // rather than a region - a pager that could name a region could answer one
    // it was never asked, which is the probe the fault path withholds.
    {
        auto decoded = decode_fault_supply_syscall(42ULL);
        if (!check(static_cast<bool>(decoded), "a valid supply was refused")) return 1;
        if (!check(decoded.value().backing == 42ULL,
                   "supply lost the backing capability")) return 1;
    }
    if (!check(refused(decode_fault_supply_syscall(0ULL),
                       fault_delivery_errors::invalid_capability),
               "supply accepted a null capability")) return 1;

    // The decoder must not invent rules that belong elsewhere: whether the
    // capability names memory, whether this pager holds it, and whether it
    // covers the region are all checked where they are enforced.
    if (!check(static_cast<bool>(decode_fault_supply_syscall(0xFFFF'FFFF'FFFF'FFFFULL)),
               "the decoder grew a rule that belongs to the capability table")) return 1;

    return 0;
}
