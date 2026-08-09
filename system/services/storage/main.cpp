#include <array>
#include <cstddef>

#include <os/core/native_handle.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/channel.hpp>
#include <os/service/bootstrap.hpp>
#include <os/service/identity.hpp>
#include <os/storage/service.hpp>

int main() {
    auto control_result = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::service::bootstrap_control_fd});
    if (!control_result) return 10;
    auto control = std::move(control_result).value();

    auto endpoint_result = os::ipc::Channel::adopt(
        os::core::NativeHandle{os::service::service_endpoint_fd});
    if (!endpoint_result) return 11;
    auto endpoint = std::move(endpoint_result).value();

    std::array<std::byte, os::ipc::max_wire_packet_size> bootstrap_buffer{};
    auto bootstrap_result = os::service::receive_bootstrap_request(
        control,
        bootstrap_buffer,
        os::storage::storage_service_id);
    if (!bootstrap_result) return 12;

    os::service::IdentityRegistry identity_registry;
    os::storage::PrivateRootRegistry roots;
    os::storage::StorageService service{
        endpoint,
        identity_registry,
        roots,
        os::storage::storage_profile_admin_principal,
    };
    service.attach_identity_control(control, identity_registry);

    auto ready_result = os::service::send_ready(
        control,
        bootstrap_result.value().request_header);
    if (!ready_result) return 13;

    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer{};
    for (;;) {
        auto dispatch = service.dispatch_once(receive_buffer, -1);
        if (!dispatch) {
            if (dispatch.error().domain == os::core::ErrorDomain::ipc &&
                dispatch.error().code == os::ipc::errors::peer_died) {
                return 0;
            }
            return 14;
        }
    }
}
