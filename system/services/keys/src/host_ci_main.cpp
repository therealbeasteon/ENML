#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

#include <poll.h>

#include <os/core/native_handle.hpp>
#include <os/ipc/constants.hpp>
#include <os/keys/control.hpp>
#include <os/keys/hierarchy.hpp>
#include <os/keys/persistence.hpp>
#include <os/keys/policy.hpp>
#include <os/keys/product_store.hpp>
#include <os/keys/random_id_source.hpp>
#include <os/keys/service.hpp>
#include <os/keys/testing/openssl_provider.hpp>
#include <os/service/bootstrap.hpp>
#include <os/service/identity.hpp>

namespace {

[[nodiscard]] bool control_readable(int fd) noexcept {
    pollfd descriptor{.fd = fd, .events = POLLIN, .revents = 0};
    int result = -1;
    do {
        result = ::poll(&descriptor, 1, 0);
    } while (result < 0 && errno == EINTR);
    return result > 0 && (descriptor.revents & POLLIN) != 0;
}

[[nodiscard]] bool control_failed(int fd) noexcept {
    pollfd descriptor{.fd = fd, .events = POLLIN, .revents = 0};
    int result = -1;
    do {
        result = ::poll(&descriptor, 1, 0);
    } while (result < 0 && errno == EINTR);
    return result > 0 && (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
}

} // namespace

int main() {
    auto control_result = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::service::bootstrap_control_fd});
    if (!control_result) return 10;
    auto control = std::move(control_result).value();

    auto endpoint_result = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::service::service_endpoint_fd});
    if (!endpoint_result) return 11;
    auto endpoint = std::move(endpoint_result).value();

    os::core::NativeHandle state_directory{os::service::service_state_directory_fd};
    if (!state_directory.valid()) return 12;

    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};
    auto bootstrap_result = os::service::receive_bootstrap_request(
        control, scratch, os::keys::key_service_id);
    if (!bootstrap_result) return 13;
    const auto bootstrap = bootstrap_result.value();

    // The service does not self-assert its own runtime identity. The Supervisor
    // owns process identity and publishes external client mappings over this
    // private control channel after READY. This IdentityRegistry starts empty
    // for the same reason system.storage does: public callers become trusted
    // only through supervisor-originated identity-control records.
    os::service::IdentityRegistry identities;

    // Host/CI-only provider. Its wrapping key and software root table are test
    // fixtures and must never be mistaken for production hardware security.
    os::keys::testing::OpenSslTestKeyProvider provider;
    os::keys::KeyHierarchy hierarchy{provider};
    const os::keys::KeyProtectionBinding system_binding{
        .scope = os::keys::KeyProtectionScope::system,
        .owner = os::keys::KeyOwner{
            .principal = bootstrap.record.identity.principal,
            .user = bootstrap.record.identity.user,
        },
    };
    auto initialized = hierarchy.initialize(system_binding);
    if (!initialized) return 14;

    auto persistent_result = os::keys::PersistentKeyRegistry::open(
        std::move(state_directory), provider);
    if (!persistent_result) return 15;
    auto persistent = std::move(persistent_result).value();

    os::keys::ApplicationKeyPolicy policy;
    os::keys::HierarchicalPolicyKeyStore product_store{persistent, hierarchy, policy};
    os::keys::RandomKeyIdSource ids;
    os::keys::KeyService service{endpoint, identities, product_store, ids};
    os::keys::KeyControlRouter router{policy, hierarchy, service, identities};

    auto ready = os::service::send_ready(control, bootstrap.request_header);
    if (!ready) return 16;

    for (;;) {
        if (control_failed(control.native_fd())) return 0;
        if (control_readable(control.native_fd())) {
            auto routed = router.dispatch_once(control, scratch);
            if (!routed) return 17;
        }

        auto dispatched = service.dispatch_once(scratch, 10);
        if (!dispatched) {
            if (dispatched.error().domain == os::core::ErrorDomain::ipc &&
                dispatched.error().code == os::ipc::errors::peer_died) {
                return 0;
            }
            return 18;
        }
    }
}
