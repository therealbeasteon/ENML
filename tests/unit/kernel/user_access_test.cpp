#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <os/kernel/aarch64_translation_root_sealer.hpp>
#include <os/kernel/address_space_epoch.hpp>
#include <os/kernel/process_translation.hpp>
#include <os/kernel/user_access.hpp>

namespace {
// Templated so a Result can be passed directly. Result's operator bool is
// explicit, which satisfies the contextual conversion in `!value` but not
// an implicit conversion to a bool parameter.
template <typename T>
void require(const T& value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;
    using namespace os::kernel::aarch64;

    alignas(4096) std::array<std::byte, 4U * 4096U> memory{};
    const auto begin = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(memory.data()));
    EarlyPageArena arena{begin, begin + memory.size()};
    EarlyStage1Builder builder{arena};
    require(builder.initialize());
    auto sealed = TranslationRootSealer::seal(builder);
    require(sealed);

    AddressSpaceEpochAuthority epochs{};
    ProcessTranslationTable translations{};
    auto epoch = epochs.acquire();
    require(epoch);
    require(translations.bind(7U, epoch.value(), sealed.value(), epochs));

    const UserRange good{.address = 0x1000ULL, .length = 64U};
    auto ticket = prepare_user_access(
        7U, epoch.value(), good, UserAccessIntent::read_from_user,
        translations, epochs);
    require(ticket && ticket.value().valid());
    require(user_access_still_live(ticket.value(), translations, epochs));

    // Initial Cookie copies are intentionally page-contained. Larger logical
    // transfers must be split into independently revalidated tickets.
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
