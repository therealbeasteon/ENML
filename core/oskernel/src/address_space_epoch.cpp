#include <os/kernel/address_space_epoch.hpp>

#include <limits>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error epoch_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

[[nodiscard]] constexpr AddressSpaceAsid asid_for_slot(std::size_t index) noexcept {
    // ASID zero stays reserved for the bootstrap/kernel translation regime.
    return static_cast<AddressSpaceAsid>(index + 1U);
}

} // namespace

os::core::Result<AddressSpaceEpoch> AddressSpaceEpochAuthority::acquire() noexcept {
    for (std::size_t i = 0U; i < slots_.size(); ++i) {
        auto& slot = slots_[i];
        if (slot.state != State::free) continue;
        if (slot.generation == std::numeric_limits<AddressSpaceGeneration>::max()) {
            return os::core::Result<AddressSpaceEpoch>{
                epoch_error(address_space_epoch_errors::generation_exhausted)};
        }
        ++slot.generation;
        if (slot.generation == 0U) {
            return os::core::Result<AddressSpaceEpoch>{
                epoch_error(address_space_epoch_errors::generation_exhausted)};
        }
        slot.state = State::active;
        ++active_;
        return AddressSpaceEpoch{
            .slot = static_cast<AddressSpaceSlot>(i),
            .generation = slot.generation,
            .asid = asid_for_slot(i),
        };
    }
    return os::core::Result<AddressSpaceEpoch>{
        epoch_error(address_space_epoch_errors::exhausted)};
}

os::core::Result<AddressSpaceEpoch> AddressSpaceEpochAuthority::resolve(
    AddressSpaceIdentity identity) const noexcept {
    if (!identity.valid() || identity.slot >= slots_.size()) {
        return os::core::Result<AddressSpaceEpoch>{
            epoch_error(address_space_epoch_errors::stale)};
    }
    const auto& slot = slots_[identity.slot];
    if (slot.state != State::active || slot.generation != identity.generation) {
        return os::core::Result<AddressSpaceEpoch>{
            epoch_error(address_space_epoch_errors::stale)};
    }
    return AddressSpaceEpoch{
        .slot = identity.slot,
        .generation = slot.generation,
        .asid = asid_for_slot(identity.slot),
    };
}

bool AddressSpaceEpochAuthority::active(AddressSpaceEpoch epoch) const noexcept {
    if (!epoch.valid() || epoch.slot >= slots_.size()) return false;
    const auto& slot = slots_[epoch.slot];
    return slot.state == State::active && slot.generation == epoch.generation &&
        epoch.asid == asid_for_slot(epoch.slot);
}

bool AddressSpaceEpochAuthority::retiring(RetiringAddressSpaceEpoch retiring_epoch) const noexcept {
    const auto epoch = retiring_epoch.epoch;
    if (!epoch.valid() || epoch.slot >= slots_.size()) return false;
    const auto& slot = slots_[epoch.slot];
    return slot.state == State::retiring && slot.generation == epoch.generation &&
        epoch.asid == asid_for_slot(epoch.slot);
}

os::core::Result<RetiringAddressSpaceEpoch> AddressSpaceEpochAuthority::begin_retire(
    AddressSpaceEpoch epoch) noexcept {
    if (!epoch.valid() || epoch.slot >= slots_.size()) {
        return os::core::Result<RetiringAddressSpaceEpoch>{
            epoch_error(address_space_epoch_errors::stale)};
    }
    auto& slot = slots_[epoch.slot];
    if (slot.generation != epoch.generation || epoch.asid != asid_for_slot(epoch.slot)) {
        return os::core::Result<RetiringAddressSpaceEpoch>{
            epoch_error(address_space_epoch_errors::stale)};
    }
    if (slot.state != State::active) {
        return os::core::Result<RetiringAddressSpaceEpoch>{
            epoch_error(address_space_epoch_errors::not_active)};
    }

    slot.state = State::retiring;
    --active_;
    ++retiring_;
    return RetiringAddressSpaceEpoch{epoch};
}

os::core::Result<void> AddressSpaceEpochAuthority::complete_retire(
    RetiringAddressSpaceEpoch retiring_epoch) noexcept {
    if (!retiring(retiring_epoch)) {
        return epoch_error(address_space_epoch_errors::not_retiring);
    }
    auto& slot = slots_[retiring_epoch.epoch.slot];
    slot.state = State::free;
    --retiring_;
    return {};
}

std::size_t AddressSpaceEpochAuthority::active_count() const noexcept { return active_; }
std::size_t AddressSpaceEpochAuthority::retiring_count() const noexcept { return retiring_; }

} // namespace os::kernel
