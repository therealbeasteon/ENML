#include <cstdlib>

#include <os/kernel/address_space_epoch.hpp>
#include <os/kernel/process_translation.hpp>
#include <os/kernel/translation_root.hpp>
#include <os/kernel/user_access.hpp>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;

    AddressSpaceEpochAuthority epochs{};
    ProcessTranslationTable translations{};
    auto epoch = epochs.acquire();
    require(epoch);

    // Test-only sealed-root token comes from the existing minted type contract.
    auto root = SealedTranslationRoot::test_only(0x4000ULL);
    require(root.valid());
    require(translations.bind(7U, epoch.value(), root, epochs));

    const UserRange good{.address = 0x1000ULL, .length = 64U};
    auto ticket = prepare_user_access(
        7U, epoch.value(), good, UserAccessIntent::read_from_user,
        translations, epochs);
    require(ticket && ticket.value().valid());
    require(user_access_still_live(ticket.value(), translations, epochs));

    require(!prepare_user_access(
        7U, epoch.value(), UserRange{.address = 0x1FF0ULL, .length = 32U},
        UserAccessIntent::read_from_user, translations, epochs));
    require(!prepare_user_access(
        7U, epoch.value(), UserRange{.address = 0ULL, .length = 1U},
        UserAccessIntent::write_to_user, translations, epochs));

    auto retiring = epochs.begin_retire(epoch.value());
    require(retiring);
    require(!user_access_still_live(ticket.value(), translations, epochs));
    require(!prepare_user_access(
        7U, epoch.value(), good, UserAccessIntent::write_to_user,
        translations, epochs));

    return 0;
}
