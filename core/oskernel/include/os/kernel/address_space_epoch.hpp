#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/result.hpp>

namespace os::kernel {

// Cookie keeps software process/principal identity separate from the hardware
// translation-cache identity. A restarted process may represent the same
// principal, but it must never silently inherit an old ASID/TLB meaning.
using AddressSpaceSlot = std::uint16_t;
using AddressSpaceGeneration = std::uint32_t;
using AddressSpaceAsid = std::uint16_t;

inline constexpr std::size_t max_address_space_epochs = 63U;
inline constexpr AddressSpaceAsid kernel_reserved_asid = 0U;

// Stable software identity for one lifetime of an address space. ASID is
// deliberately absent: it is a hardware translation-cache tag and is reused
// after the old translations have been invalidated. Security policy must bind to
// this identity, never to ASID alone.
struct AddressSpaceIdentity final {
    AddressSpaceSlot slot {0U};
    AddressSpaceGeneration generation {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return slot < max_address_space_epochs && generation != 0U;
    }

    [[nodiscard]] friend constexpr bool operator==(
        const AddressSpaceIdentity&, const AddressSpaceIdentity&) = default;
};

struct AddressSpaceEpoch final {
    AddressSpaceSlot slot {0U};
    AddressSpaceGeneration generation {0U};
    AddressSpaceAsid asid {kernel_reserved_asid};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return slot < max_address_space_epochs && generation != 0U &&
               asid != kernel_reserved_asid;
    }

    [[nodiscard]] constexpr AddressSpaceIdentity identity() const noexcept {
        if (!valid()) return {};
        return AddressSpaceIdentity{slot, generation};
    }

    [[nodiscard]] friend constexpr bool operator==(
        const AddressSpaceEpoch&, const AddressSpaceEpoch&) = default;
};

struct RetiringAddressSpaceEpoch final {
    AddressSpaceEpoch epoch {};

    [[nodiscard]] constexpr bool valid() const noexcept { return epoch.valid(); }
};

namespace address_space_epoch_errors {
inline constexpr std::uint32_t exhausted = 1U;
inline constexpr std::uint32_t stale = 2U;
inline constexpr std::uint32_t not_active = 3U;
inline constexpr std::uint32_t not_retiring = 4U;
inline constexpr std::uint32_t generation_exhausted = 5U;
} // namespace address_space_epoch_errors

// Bounded ASID lifecycle authority.
//
// State transitions are deliberately one-way:
//   free -> active -> retiring -> free
//
// begin_retire() invalidates the software epoch immediately so no new thread may
// bind to it. complete_retire() is intentionally a separate operation because
// the machine layer must first perform the architecturally required TLB
// invalidation. Until completion, the ASID remains quarantined and cannot be
// handed to another address space.
class AddressSpaceEpochAuthority final {
public:
    [[nodiscard]] os::core::Result<AddressSpaceEpoch> acquire() noexcept;
    [[nodiscard]] os::core::Result<RetiringAddressSpaceEpoch> begin_retire(
        AddressSpaceEpoch epoch) noexcept;
    [[nodiscard]] os::core::Result<void> complete_retire(
        RetiringAddressSpaceEpoch retiring) noexcept;

    [[nodiscard]] bool active(AddressSpaceEpoch epoch) const noexcept;
    [[nodiscard]] bool retiring(RetiringAddressSpaceEpoch epoch) const noexcept;
    [[nodiscard]] std::size_t active_count() const noexcept;
    [[nodiscard]] std::size_t retiring_count() const noexcept;

private:
    enum class State : std::uint8_t { free = 0U, active = 1U, retiring = 2U };
    struct Slot final {
        AddressSpaceGeneration generation {0U};
        State state {State::free};
    };

    std::array<Slot, max_address_space_epochs> slots_ {};
    std::size_t active_ {0U};
    std::size_t retiring_ {0U};
};

} // namespace os::kernel
