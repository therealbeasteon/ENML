#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>
#include <os/kernel/capability.hpp>
#include <os/kernel/rendezvous.hpp>

// The Cookie Kernel's interrupt dispatch core, as a state machine.
//
// The third of the four kernel responsibilities in `docs/M7_0_KERNEL.md`, and
// the last one that is pure logic - so, like the rendezvous and the capability
// table, it is written with no hardware in it and tested on a development host.
// `core/oskernel/include/os/kernel/machine.hpp` owns the half that needs a
// machine: masking a source, unmasking it, acknowledging it. This file decides
// *when* those happen and to whom the interrupt belongs.
//
// `docs/M6_0_DEVICE_ACCESS.md` deferred interrupt authority on the grounds that
// it needed its own threat model rather than half a specification.
// `docs/M7_1_INTERRUPT.md` is that threat model; what follows is the part that
// has to be true in code.
//
// The references pull in two directions here, and ENML ends up at neither.
//
// The microkernel that carried an entire operating system connected a handler
// *inside a user process* to a vector, and the kernel called it in interrupt
// context. The handler accumulated work - characters into a ring buffer - and
// woke the owning process only when something worth waking it for had happened.
// That is what let it drive non-intelligent serial hardware at rates a
// monolithic system of the day could not, and the technique is sound.
//
// The same reference set records what that costs. Driver code running with
// interrupts off, able to crash there, leaves the kernel unable to begin
// recovery - the failure the microdriver work documents when a user-level
// driver takes a lock, disables interrupts and dies. And a system that dispatches
// interrupts as fast as a device raises them stops running the process that
// services them at all: receive livelock, where the machine is fully occupied
// taking interrupts and makes no progress on any of them.
//
// So Cookie takes the goal and refuses both mechanisms:
//
//   - **No user code ever runs in interrupt context.** Not a handler, not a
//     first-level filter, nothing. Dispatch marks the source, masks it and makes
//     the owning driver thread runnable; the driver is an ordinary thread doing
//     ordinary work under the M6.0 device policy, and a defect in it is a
//     process fault the supervisor restarts rather than a fault at interrupt
//     time.
//
//   - **The kernel coalesces, and says how much it coalesced.** The reason for
//     an in-kernel handler was to avoid waking the process once per interrupt.
//     Counting assertions that arrive while one is outstanding buys the same
//     reduction: the driver wakes once per burst and is told the burst's size,
//     so its own significant-event logic still works. What it does not buy is a
//     place for driver code to execute at interrupt time.
//
//   - **A source is masked from the moment it is dispatched until its driver
//     says the device is quiet.** At most one interrupt is ever in flight per
//     source. This is the structural answer to receive livelock: interrupt load
//     is bounded by how fast the driver makes progress rather than by how fast
//     the device asserts, so a device that raises its line continuously cannot
//     take the machine away from anyone. It is also why the ABI marks
//     `interrupt_complete` non-blocking - a driver telling the kernel its device
//     is quiet must never be made to wait for the privilege.
//
// The cost, stated rather than discovered: Cookie pays a thread wakeup per burst
// where the reference design paid one per significant event, and a driver that
// needs a timestamp per individual interrupt cannot have one. Both are accepted.
// A phone's interrupt sources are not a 20 MHz UART, and neither is worth a
// place for third-party code to run with the machine's interrupts held off.
namespace os::kernel {

// A ceiling on *attached* sources, not on what a controller can number. The
// kernel needs an entry only for a source somebody owns, so this bounds the
// table without bounding the hardware.
inline constexpr std::size_t max_interrupt_sources = 64U;

// Zero is never a valid source, on the same principle as thread and capability
// identifiers: a zeroed register must not name one.
//
// This costs no hardware line, because these are not controller interrupt
// numbers. Source numbers are the kernel's own namespace and the machine layer
// translates them, which is what keeps `interrupt_attach` from being a call
// whose meaning changes per board.
using InterruptSource = std::uint32_t;
inline constexpr InterruptSource invalid_interrupt_source = 0U;

// A capability minted with the wrong object tag must never be mistaken for
// authority over a source, even if the numeric ordinal happens to collide -
// the same reasoning ipc_object_tag documents for IPC endpoints.
inline constexpr ObjectId interrupt_object_tag = 0x1EC0'0000'0000'0000ULL;
inline constexpr ObjectId interrupt_object_tag_mask = 0xFFFF'0000'0000'0000ULL;

[[nodiscard]] constexpr ObjectId interrupt_object_id(InterruptSource source) noexcept {
    if (source == invalid_interrupt_source) return invalid_object;
    return interrupt_object_tag | static_cast<ObjectId>(source);
}

// One right, because there is one operation a capability over a source
// authorizes: owning it. Not modeled after ipc_right_send/ipc_right_receive's
// two-sided split - IPC has a sender and a receiver with genuinely different
// authority; a source has exactly one owner doing exactly one kind of thing
// to it, and a second right would be a distinction with no operation behind
// it.
inline constexpr Rights interrupt_right_attach = 1U << 0U;

namespace interrupt_errors {
inline constexpr std::uint32_t invalid_source = 1U;
inline constexpr std::uint32_t invalid_driver = 2U;
// Another driver already owns this source. Sharing is refused; see below.
inline constexpr std::uint32_t source_taken = 3U;
inline constexpr std::uint32_t not_attached = 4U;
inline constexpr std::uint32_t not_owner = 5U;
inline constexpr std::uint32_t source_limit = 6U;
// Beginning service on a source with nothing outstanding.
inline constexpr std::uint32_t not_pending = 7U;
// Ending service on a source that is not being serviced.
inline constexpr std::uint32_t not_in_service = 8U;
// The capability-check codes below belong to the Kernel composition layer
// (interrupt_attach/interrupt_detach/interrupt_complete), not to
// InterruptTable itself, which never sees a capability - see the composition
// note above. They live here rather than in a new namespace because they are
// still about interrupt authority, the same way ipc_errors carries both
// IPC-specific codes and the capability-check codes IpcEndpointTable's own
// composition surface returns.
inline constexpr std::uint32_t invalid_capability = 9U;
inline constexpr std::uint32_t wrong_rights = 10U;
inline constexpr std::uint32_t wrong_object = 11U;
} // namespace interrupt_errors

enum class InterruptState : std::uint8_t {
    // Owned, unmasked, nothing outstanding. The device may interrupt.
    attached = 1U,
    // Asserted and masked; the owner has not collected it yet.
    pending = 2U,
    // The owner is working on it, and the source is still masked.
    in_service = 3U,
};

// What the first-level handler should do about an interrupt it just took.
struct Dispatch final {
    // Whom to make runnable, or invalid_thread when there is nobody to wake.
    ThreadId owner {invalid_thread};
    // True on the assertion that starts a burst. False when the owner is
    // already awake or already working, because waking a running thread is not
    // an event and counting it as one is how a wakeup budget stops meaning
    // anything.
    bool wake {false};
    // An interrupt was already outstanding, so this one was folded into the
    // count rather than delivered separately.
    bool coalesced {false};

    [[nodiscard]] friend constexpr bool operator==(const Dispatch&, const Dispatch&) = default;
};

// What the driver learns when it collects an interrupt.
struct Service final {
    // How many assertions this covers, always at least one. A driver that sees
    // more than one knows the device asserted while it was masked and that a
    // rescan is required - the information an edge-triggered source would
    // otherwise have destroyed.
    std::uint32_t assertions {0U};
    // True when the count stopped rising because it hit its ceiling, so a
    // driver can tell "exactly this many" from "at least this many".
    bool saturated {false};

    [[nodiscard]] friend constexpr bool operator==(const Service&, const Service&) = default;
};

class InterruptTable final {
public:
    InterruptTable() noexcept = default;

    // Gives a driver thread exclusive ownership of a source.
    //
    // Whether the caller is *allowed* this source is decided above, against the
    // capability it holds - the same separation the capability table keeps from
    // the rendezvous. Two state machines that each know about the other are two
    // state machines neither of which can be tested alone.
    //
    // Sharing is refused rather than supported. A shared line turns
    // mask-until-complete into mask-until-everyone-completes, which lets one
    // slow or broken driver hold the line down for a device it has nothing to do
    // with. Bus-level sharing is a legacy of expansion slots; an SoC assigns its
    // interrupts, and inheriting the problem to be general would be inheriting
    // it for nothing.
    [[nodiscard]] os::core::Result<void> attach(
        ThreadId driver,
        InterruptSource source) noexcept;

    // Gives up a source. Only its owner may.
    //
    // The source is left masked. An owner-less source that can still assert is a
    // livelock nobody is positioned to stop; an owner-less source that cannot is
    // a device that has gone quiet. Between a dead device and a dead system the
    // choice is not close.
    [[nodiscard]] os::core::Result<void> detach(
        ThreadId driver,
        InterruptSource source) noexcept;

    // The kernel's first-level handler took an interrupt for this source.
    //
    // Never refuses for a reason the hardware could arrange: an interrupt on a
    // source nobody owns is counted as spurious and reported, because a line
    // asserting with no driver behind it is a hardware or configuration fault
    // and this count is the only evidence anyone will get.
    os::core::Result<Dispatch> dispatch(InterruptSource source) noexcept;

    // The owner collects what is outstanding and starts working.
    //
    // Deliberately not a system call - it is the transition the kernel performs
    // when it makes the driver runnable, and the count rides back on the wakeup.
    // Adding a call so a driver could ask would grow the surface by one for
    // information it is about to be handed anyway.
    [[nodiscard]] os::core::Result<Service> begin_service(
        ThreadId driver,
        InterruptSource source) noexcept;

    // The driver says the device is quiet. This is what `interrupt_complete`
    // invokes.
    //
    // Returns true when the source must be serviced again immediately, because
    // it asserted while the driver was working. Without that answer the
    // assertion is simply lost, and a driver that unmasks into an already-raised
    // level-triggered line either spins or stalls forever depending on which way
    // the race fell.
    os::core::Result<bool> end_service(ThreadId driver, InterruptSource source) noexcept;

    // Releases every source a thread owns, leaving each masked.
    //
    // Called when a driver dies. Asks no permission, because it is not a thread
    // exercising authority - it is the kernel meeting an obligation for a thread
    // that no longer exists, exactly as the rendezvous releases everyone blocked
    // on a dead thread and the capability table surrenders what it held.
    std::size_t detach_all_owned_by(ThreadId driver) noexcept;

    [[nodiscard]] os::core::Result<InterruptState> state_of(InterruptSource source) const noexcept;
    [[nodiscard]] os::core::Result<ThreadId> owner_of(InterruptSource source) const noexcept;
    // Whether the source is masked at the controller, as this table believes it
    // to be. The machine layer is told; this is what it was told.
    [[nodiscard]] os::core::Result<bool> is_masked(InterruptSource source) const noexcept;

    [[nodiscard]] std::size_t attached_source_count() const noexcept;
    // Interrupts taken for sources nobody owns, saturating rather than wrapping.
    [[nodiscard]] std::uint32_t spurious_count() const noexcept;

private:
    struct Slot final {
        InterruptSource source {invalid_interrupt_source};
        ThreadId owner {invalid_thread};
        InterruptState state {InterruptState::attached};
        // Assertions folded into the outstanding one, including the first.
        std::uint32_t assertions {0U};
        bool saturated {false};
        bool occupied {false};
    };

    [[nodiscard]] Slot* find(InterruptSource source) noexcept;
    [[nodiscard]] const Slot* find(InterruptSource source) const noexcept;

    std::array<Slot, max_interrupt_sources> slots_ {};
    std::size_t occupied_ {0U};
    std::uint32_t spurious_ {0U};
};

} // namespace os::kernel
