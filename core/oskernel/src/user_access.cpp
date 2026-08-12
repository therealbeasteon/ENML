#include <os/kernel/user_access.hpp>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {
[[nodiscard]] constexpr os::core::Error user_access_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}
}

os::core::Result<UserAccessTicket> prepare_user_access(
    ThreadId thread,
    AddressSpaceEpoch expected_epoch,
    UserRange range,
    UserAccessIntent intent,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    if (!range.valid()) return user_access_error(user_access_errors::invalid_range);

    auto binding = translations.resolve(thread, epochs);
    if (!binding) return user_access_error(user_access_errors::stale_translation);
    if (!(binding.value().epoch == expected_epoch)) {
        return user_access_error(user_access_errors::epoch_mismatch);
    }

    return UserAccessTicket{
        .thread = thread,
        .epoch = binding.value().epoch,
        .root_physical = binding.value().root_physical,
        .range = range,
        .intent = intent,
    };
}

bool user_access_still_live(
    const UserAccessTicket& ticket,
    const ProcessTranslationTable& translations,
    const AddressSpaceEpochAuthority& epochs) noexcept {
    if (!ticket.valid()) return false;
    auto binding = translations.resolve(ticket.thread, epochs);
    if (!binding) return false;
    return binding.value().epoch == ticket.epoch &&
        binding.value().root_physical == ticket.root_physical;
}

} // namespace os::kernel
