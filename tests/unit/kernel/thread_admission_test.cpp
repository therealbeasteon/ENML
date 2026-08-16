#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <os/core/error.hpp>
#include <os/kernel/aarch64_translation_root_sealer.hpp>
#include <os/kernel/abi.hpp>
#include <os/kernel/kernel.hpp>
#include <os/kernel/thread_admission.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) std::fprintf(stderr, "thread admission: %s\n", what);
    return condition;
}

template <typename T>
bool refused(const os::core::Result<T>& result, std::uint32_t code) {
    return !result && result.error().domain == os::core::ErrorDomain::kernel &&
           result.error().code == code;
}

constexpr os::kernel::ThreadId creator = 3U;
constexpr os::kernel::ThreadId stranger = 4U;
constexpr std::uint64_t program_entry = 0x0040'0000ULL;
constexpr std::uint64_t stack_top = 0x0080'0000ULL;

} // namespace

// Admitting a thread into an address space. The properties worth testing are
// the ones docs/M7_12_ENTRY_BINDING.md decides: that the entry comes from the
// space and not from the caller, that the identifier is issued rather than
// accepted and never reused, that admission is its own right, and that a
// thread is never runnable before it has architectural state.
int main() {
    using namespace os::kernel;
    using namespace os::kernel::aarch64;

    // The entry is bound in the seal, so a seal that cannot produce a usable
    // one is refused before the builder is burned. Sealing is irreversible: a
    // builder consumed by a rejected entry could never be sealed again.
    alignas(4096) std::array<std::byte, 12U * 4096U> memory{};
    const auto begin =
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(memory.data()));
    EarlyPageArena arena{begin, begin + memory.size()};
    EarlyStage1Builder builder{arena};
    if (!check(static_cast<bool>(builder.initialize()), "builder did not initialize")) return 1;

    if (!check(refused(TranslationRootSealer::seal(builder, 0ULL),
                       translation_root_errors::invalid_entry),
               "a zero entry was sealed")) return 1;
    // Four-byte alignment is not housekeeping: an unaligned entry names a point
    // inside an A64 instruction, which is the smallest possible version of
    // entering signed code somewhere it did not intend to be entered.
    if (!check(refused(TranslationRootSealer::seal(builder, program_entry + 1U),
                       translation_root_errors::invalid_entry),
               "an entry inside an instruction was sealed")) return 1;
    if (!check(refused(TranslationRootSealer::seal(builder, kernel_virtual_base),
                       translation_root_errors::invalid_entry),
               "a kernel-region entry was sealed")) return 1;
    // Rejected three times and still sealable, which is the point of checking
    // the entry first.
    auto root = TranslationRootSealer::seal(builder, program_entry);
    if (!check(static_cast<bool>(root), "seal refused a good entry")) return 1;
    if (!check(root.value().entry() == program_entry, "the seal lost the entry")) return 1;

    Kernel kernel{};
    AddressSpaceEpochAuthority epochs{};
    ProcessTranslationTable translations{};
    if (!check(static_cast<bool>(kernel.create_thread(creator, 7U)),
               "the creator could not be created")) return 1;
    if (!check(static_cast<bool>(kernel.create_thread(stranger, 7U)),
               "the stranger could not be created")) return 1;

    auto authority = kernel.capabilities().mint(
        creator, address_space_authority_object, address_space_right_create, false);
    if (!check(static_cast<bool>(authority), "the create authority was refused")) return 1;
    auto created = kernel.address_space_create(creator, authority.value(), epochs);
    if (!check(static_cast<bool>(created), "address_space_create refused")) return 1;

    // A created space's capability carries all three rights, because its
    // creator built it and there is nobody else to hold any of them yet.
    auto described = kernel.capabilities().describe(created.value().capability);
    if (!check(static_cast<bool>(described), "the space capability had no description")) return 1;
    if (!check((described.value().rights & address_space_right_admit) != 0U,
               "the creator was not given the right to admit")) return 1;

    // A stack of zero names nothing. It is the only thing this layer checks of
    // it - whether it is mapped is the fault path's question.
    if (!check(refused(kernel.thread_admit(creator, created.value().capability, 0ULL,
                                           root.value(), epochs, translations),
                       thread_admission_errors::invalid_stack),
               "a thread was admitted with no stack")) return 1;

    // Holding the space is not being allowed to put a thread in it. This is the
    // pager case: it services faults in spaces it does not own, and if `hold`
    // implied admission it could run code of its choosing inside every process
    // it pages for.
    auto held = kernel.capabilities().grant(
        creator, created.value().capability, stranger, address_space_right_hold, false);
    if (!check(static_cast<bool>(held), "hold could not be derived")) return 1;
    if (!check(refused(kernel.thread_admit(stranger, held.value(), stack_top,
                                           root.value(), epochs, translations),
                       address_space_syscall_errors::invalid_capability),
               "a holder without the admit right admitted a thread")) return 1;

    const auto threads_before = kernel.live_thread_count();

    auto admitted = kernel.thread_admit(
        creator, created.value().capability, stack_top, root.value(), epochs, translations);
    if (!check(static_cast<bool>(admitted), "admission refused")) return 1;
    if (!check(admitted.value().valid(), "admission returned nothing usable")) return 1;

    // The entry is the space's, and the caller never named it. There is no
    // argument it could have passed to change this value.
    if (!check(admitted.value().entry == program_entry,
               "the admitted thread did not start where the space said")) return 1;
    if (!check(admitted.value().stack == stack_top, "the stack was not the caller's")) return 1;

    // Issued, not accepted, and out of the reserved range so it cannot collide
    // with a thread boot made by hand.
    const auto admitted_thread = admitted.value().thread;
    if (!check(admitted_thread >= first_admitted_thread,
               "an issued identifier landed below the reserved floor")) return 1;
    if (!check(admitted_thread != creator && admitted_thread != stranger,
               "an issued identifier collided with a live thread")) return 1;
    if (!check(kernel.live_thread_count() == threads_before + 1U,
               "the admitted thread was not tracked")) return 1;

    // Bound to the space, so a capability minted for it is bound to the pair
    // rather than to the thread alone.
    auto binding = translations.resolve(admitted_thread, epochs);
    if (!check(static_cast<bool>(binding), "the admitted thread was not bound")) return 1;
    if (!check(binding.value().epoch == created.value().epoch,
               "the thread was bound to a different space")) return 1;
    if (!check(binding.value().authority().valid(), "the binding produced no authority")) return 1;

    // Not runnable. The machine layer starts it once it has admitted an
    // exception frame; the reverse order leaves a window in which the scheduler
    // may select a thread whose architectural state was never written.
    auto runnable = kernel.runqueue().is_runnable(admitted_thread);
    if (!check(static_cast<bool>(runnable), "the admitted thread was not in the runqueue")) return 1;
    if (!check(!runnable.value(),
               "the admitted thread was runnable before it had a frame")) return 1;
    if (!check(kernel.threads().state_of(admitted_thread).value() == ThreadState::admitted,
               "the admitted thread was not in the admitted state")) return 1;

    // And it stays not runnable across unrelated kernel activity. This is the
    // regression hardware found: the first version only dequeued the thread,
    // and synchronise_thread recomputes runnability from the rendezvous state
    // on every IPC operation, so the next send anywhere in the system put a
    // frameless thread back in the runqueue - ahead of everything else,
    // because it had never run.
    if (!check(static_cast<bool>(kernel.send(creator, stranger)),
               "the unrelated send was refused")) return 1;
    if (!check(!kernel.runqueue().is_runnable(admitted_thread).value(),
               "an unrelated IPC operation made a frameless thread runnable")) return 1;

    // thread_start is the one way out, and only from `admitted`.
    if (!check(static_cast<bool>(kernel.thread_start(admitted_thread)),
               "thread_start refused an admitted thread")) return 1;
    if (!check(kernel.runqueue().is_runnable(admitted_thread).value(),
               "a started thread was still not runnable")) return 1;
    if (!check(!kernel.thread_start(admitted_thread),
               "thread_start ran twice on the same thread")) return 1;
    if (!check(!kernel.thread_start(creator),
               "thread_start started a thread that was never admitted")) return 1;
    // The creator's priority, not a caller's choice.
    if (!check(kernel.threads().effective_priority_of(admitted_thread).value() == 7U,
               "the admitted thread did not inherit its creator's priority")) return 1;

    // One thread per space today - ProcessTranslationTable refuses a second
    // binding to the same epoch - and the refusal must not leak a thread.
    const auto after_first = kernel.live_thread_count();
    if (!check(!kernel.thread_admit(creator, created.value().capability, stack_top,
                                    root.value(), epochs, translations),
               "a second thread was admitted to one space")) return 1;
    if (!check(kernel.live_thread_count() == after_first,
               "a refused admission left a thread behind")) return 1;

    // Identifiers are never reused, so the refused attempt above spent one and
    // the next admission gets a different number than the first.
    ThreadIdentifierIssuer issuer{};
    auto first = issuer.issue();
    auto second = issuer.issue();
    if (!check(first && second, "the issuer refused")) return 1;
    if (!check(first.value() == first_admitted_thread, "issuing did not start at the floor")) return 1;
    if (!check(second.value() != first.value(), "an identifier was reused")) return 1;

    // A capability over a destroyed space does not admit into its successor.
    // Nothing here is a staleness check written for the purpose: the generation
    // is part of the identity the capability carries.
    auto retiring = kernel.address_space_begin_destroy(
        creator, created.value().capability, epochs);
    if (!check(static_cast<bool>(retiring), "begin_destroy refused")) return 1;
    if (!check(!kernel.thread_admit(creator, created.value().capability, stack_top,
                                    root.value(), epochs, translations),
               "a thread was admitted into a retiring space")) return 1;

    // The decoder. Two arguments, and zero is the only value it can reject.
    auto decoded = decode_thread_create_syscall(9ULL, stack_top);
    if (!check(static_cast<bool>(decoded), "a valid thread_create was refused")) return 1;
    if (!check(decoded.value().space == 9U && decoded.value().stack == stack_top,
               "the decoder lost an argument")) return 1;
    if (!check(refused(decode_thread_create_syscall(0ULL, stack_top),
                       thread_admission_errors::invalid_capability),
               "a thread_create naming no space was decoded")) return 1;
    if (!check(refused(decode_thread_create_syscall(9ULL, 0ULL),
                       thread_admission_errors::invalid_stack),
               "a thread_create with no stack was decoded")) return 1;

    // The ABI says two arguments, and the two it does not take are the design.
    auto descriptor = describe_call(KernelCall::thread_create);
    if (!check(static_cast<bool>(descriptor), "thread_create is not in the call table")) return 1;
    if (!check(descriptor.value().argument_count == 2U,
               "thread_create took a different number of arguments")) return 1;
    if (!check(descriptor.value().authority == CallAuthority::process_control,
               "thread_create carried the wrong authority")) return 1;

    return 0;
}
