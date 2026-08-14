#include <array>
#include <cstddef>
#include <cstdint>

#include <os/kernel/capability.hpp>

// The capability model - mint, grant, revoke, describe, holds, both the
// legacy ThreadId-only surface and M7.8's ExecutionAuthority-bound one - was
// the other gap docs/ROADMAP.md named after M7.6a/M7.8 landed. Unlike
// fuzz/kernel/ipc_syscall_fuzz.cpp's decoders, CapabilityTable is a state
// machine, not a stateless parser: the bugs worth finding here are in
// transitions between operations (derive past max_derivation_depth, revoke
// racing a grant, describe after teardown), not in a single call's argument
// validation. So this interprets the input as a short script of operations
// against one table rather than decoding it as one shape.
namespace {

using os::kernel::AddressSpaceGeneration;
using os::kernel::AddressSpaceIdentity;
using os::kernel::AddressSpaceSlot;
using os::kernel::CapabilityId;
using os::kernel::CapabilityTable;
using os::kernel::ExecutionAuthority;
using os::kernel::ObjectId;
using os::kernel::Rights;
using os::kernel::ThreadId;

// A small, dense pool for every identifier kind this state machine takes.
// Fuzzing a 256-slot table against the full 64-bit id space would spend
// almost every operation rediscovering "unknown capability" instead of
// exercising the transitions that actually interact - derivation, grant,
// revoke, teardown - between a handful of actors sharing a handful of
// objects, which is where this table's own invariants are actually tested.
constexpr std::size_t pool_size = 4U;

[[nodiscard]] ThreadId pooled_thread(std::uint8_t raw) noexcept {
    return static_cast<ThreadId>((raw % pool_size) + 1U);
}

[[nodiscard]] ObjectId pooled_object(std::uint8_t raw) noexcept {
    return static_cast<ObjectId>((raw % pool_size) + 1U);
}

[[nodiscard]] AddressSpaceIdentity pooled_identity(std::uint8_t raw) noexcept {
    return AddressSpaceIdentity{
        static_cast<AddressSpaceSlot>(raw % pool_size),
        static_cast<AddressSpaceGeneration>((raw % pool_size) + 1U)};
}

class ByteCursor final {
public:
    ByteCursor(const std::uint8_t* data, std::size_t size) noexcept
        : data_(data), size_(size) {}

    [[nodiscard]] std::uint8_t next() noexcept {
        if (offset_ >= size_) return 0U;
        return data_[offset_++];
    }

    [[nodiscard]] bool exhausted() const noexcept { return offset_ >= size_; }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t offset_ {0U};
};

// Every id this table has ever handed out gets a chance to be reused by a
// later operation - grant-after-revoke, describe-after-teardown, a second
// derivation from the same parent - alongside a raw fuzzed value so unknown
// and invalid ids stay covered too. A fuzzer that only ever fabricates fresh
// ids never reaches the transitions between two operations on the same
// capability, which is where this table's invariants actually live.
class CapabilityLedger final {
public:
    void remember(CapabilityId id) noexcept {
        if (id == os::kernel::invalid_capability) return;
        seen_[next_ % seen_.size()] = id;
        ++next_;
        if (count_ < seen_.size()) ++count_;
    }

    [[nodiscard]] CapabilityId pick(std::uint8_t raw, CapabilityId fallback) const noexcept {
        if (count_ == 0U) return fallback;
        return seen_[raw % count_];
    }

private:
    std::array<CapabilityId, 8U> seen_ {};
    std::size_t next_ {0U};
    std::size_t count_ {0U};
};

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    ByteCursor cursor{data, size};
    CapabilityTable table;
    CapabilityLedger ledger;

    // Bounded independent of input size. This table's own storage
    // (max_capabilities) is fixed, so a script longer than a couple hundred
    // operations spends fuzzer time re-proving the limit is enforced rather
    // than finding new transitions.
    constexpr std::size_t max_operations = 128U;
    for (std::size_t step = 0U; step < max_operations && !cursor.exhausted(); ++step) {
        const auto opcode = static_cast<std::uint8_t>(cursor.next() % 10U);
        const auto raw_a = cursor.next();
        const auto raw_b = cursor.next();
        const auto raw_c = cursor.next();
        const auto raw_d = cursor.next();

        switch (opcode) {
        case 0U: {
            auto minted = table.mint(
                pooled_thread(raw_a), pooled_object(raw_b),
                static_cast<Rights>(raw_c), (raw_d & 1U) != 0U);
            if (minted) ledger.remember(minted.value());
            break;
        }
        case 1U: {
            const auto authority =
                ExecutionAuthority{pooled_thread(raw_a), pooled_identity(raw_b)};
            auto minted = table.mint(
                authority, pooled_object(raw_c), static_cast<Rights>(raw_d), false);
            if (minted) ledger.remember(minted.value());
            break;
        }
        case 2U: {
            const auto cap = ledger.pick(raw_b, static_cast<CapabilityId>(raw_a));
            auto granted = table.grant(
                pooled_thread(raw_a), cap, pooled_thread(raw_c),
                static_cast<Rights>(raw_d), (raw_d & 1U) != 0U);
            if (granted) ledger.remember(granted.value());
            break;
        }
        case 3U: {
            const auto authority =
                ExecutionAuthority{pooled_thread(raw_a), pooled_identity(raw_b)};
            const auto recipient =
                ExecutionAuthority{pooled_thread(raw_c), pooled_identity(raw_d)};
            const auto cap = ledger.pick(raw_c, static_cast<CapabilityId>(raw_a));
            auto granted = table.grant(
                authority, cap, recipient, static_cast<Rights>(raw_d), false);
            if (granted) ledger.remember(granted.value());
            break;
        }
        case 4U: {
            const auto cap = ledger.pick(raw_b, static_cast<CapabilityId>(raw_a));
            (void)table.revoke(pooled_thread(raw_a), cap);
            break;
        }
        case 5U: {
            const auto authority =
                ExecutionAuthority{pooled_thread(raw_a), pooled_identity(raw_b)};
            const auto cap = ledger.pick(raw_c, static_cast<CapabilityId>(raw_a));
            (void)table.revoke(authority, cap);
            break;
        }
        case 6U: {
            const auto cap = ledger.pick(raw_b, static_cast<CapabilityId>(raw_a));
            (void)table.holds(pooled_thread(raw_a), cap);
            break;
        }
        case 7U: {
            const auto authority =
                ExecutionAuthority{pooled_thread(raw_a), pooled_identity(raw_b)};
            const auto cap = ledger.pick(raw_c, static_cast<CapabilityId>(raw_a));
            (void)table.holds(authority, cap);
            break;
        }
        case 8U: {
            const auto cap = ledger.pick(raw_a, static_cast<CapabilityId>(raw_a));
            (void)table.describe(cap);
            break;
        }
        default: {
            (void)table.revoke_all_held_by(pooled_thread(raw_a));
            break;
        }
        }
    }

    (void)table.live_capability_count();
    return 0;
}
