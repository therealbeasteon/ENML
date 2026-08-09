#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>

#include <poll.h>

#include <os/core/error.hpp>
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

[[nodiscard]] bool peer_died(const os::core::Error& error) noexcept {
    return error.domain == os::core::ErrorDomain::ipc &&
        error.code == os::ipc::errors::peer_died;
}

void report_fatal(const char* stage, const os::core::Error& error) noexcept {
    std::fprintf(
        stderr,
        "system.keys fatal stage=%s domain=%u code=%u\n",
        stage,
        static_cast<unsigned>(error.domain),
        static_cast<unsigned>(error.code));
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

    std::array<std::byte, os::ipc::max_wire_packet_size> bootstrap_buffer{};
    auto bootstrap_result = os::service::receive_bootstrap_request(
        control, bootstrap_buffer, os::keys::key_service_id);
    if (!bootstrap_result) return 13;
    const auto bootstrap = bootstrap_result.value();

    // The service never self-asserts runtime identity. The Supervisor owns the
    // mapping and publishes live external processes over the private control
    // channel after READY, exactly as for other supervised system services.
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

    // Keep control and public receive storage separate. Besides making packet
    // lifetimes obvious, this mirrors the already-proven system.storage event
    // loop and keeps privileged lifecycle messages distinct from app RPC bytes.
    std::array<std::byte, os::ipc::max_wire_packet_size> control_buffer{};
    std::array<std::byte, os::ipc::max_wire_packet_size> request_buffer{};

    for (;;) {
        pollfd control_poll{.fd = control.native_fd(), .events = POLLIN, .revents = 0};
        int polled = -1;
        do {
            polled = ::poll(&control_poll, 1, 0);
        } while (polled < 0 && errno == EINTR);
        if (polled < 0) return 17;

        if ((control_poll.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
            (control_poll.revents & POLLIN) == 0) {
            return 0;
        }
        if ((control_poll.revents & POLLIN) != 0) {
            auto routed = router.dispatch_once(control, control_buffer);
            if (!routed) {
                if (peer_died(routed.error())) return 0;
                report_fatal("control", routed.error());
                return 18;
            }
        }

        auto dispatched = service.dispatch_once(request_buffer, 10);
        if (!dispatched) {
            if (peer_died(dispatched.error())) return 0;
            report_fatal("public", dispatched.error());
            return 19;
        }
    }
}
