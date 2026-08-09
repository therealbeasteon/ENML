#include <os/app/bootstrap.hpp>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

#include <poll.h>

#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>
#include <os/ipc/rpc.hpp>

namespace os::app {
namespace {

inline constexpr os::core::RequestId bootstrap_request_id_v2{1U};

[[nodiscard]] constexpr os::core::Error app_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

[[nodiscard]] constexpr os::core::Error ipc_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ipc, code);
}

[[nodiscard]] constexpr bool has_flag(std::uint32_t flags, os::ipc::WireFlag flag) noexcept {
    return (flags & os::ipc::flag_value(flag)) != 0U;
}

[[nodiscard]] constexpr bool valid_record(const ApplicationBootstrapRecordV1& record) noexcept {
    return record.instance.value() != 0U &&
        os::core::valid_peer_identity(record.identity) &&
        record.package_generation != 0U;
}

[[nodiscard]] bool valid_services(std::span<const os::core::ServiceId> services) noexcept {
    if (services.empty() || services.size() > max_application_service_endpoints_v2) return false;
    for (std::size_t index = 0U; index < services.size(); ++index) {
        if (services[index].value() == 0U) return false;
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (services[prior] == services[index]) return false;
        }
    }
    return true;
}

[[nodiscard]] os::core::Result<std::size_t>
encode_payload_v2(
    os::core::MutableByteSpan output,
    const ApplicationBootstrapRecordV1& record,
    std::span<const os::core::ServiceId> services) noexcept {
    if (!valid_record(record) || !valid_services(services)) {
        return app_error(os::core::errors::service::invalid_bootstrap);
    }
    const std::size_t payload_size = application_bootstrap_payload_base_size_v2 +
        (services.size() * application_bootstrap_service_entry_size_v2);
    if (payload_size > output.size() ||
        payload_size > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) {
        return ipc_error(os::ipc::errors::buffer_too_small);
    }

    os::ipc::Encoder encoder{output.first(payload_size)};
    auto result = encoder.write_u16_le(application_bootstrap_version_v2);
    if (!result) return result.error();
    result = encoder.write_u16_le(static_cast<std::uint16_t>(payload_size));
    if (!result) return result.error();
    result = encoder.write_u16_le(static_cast<std::uint16_t>(services.size()));
    if (!result) return result.error();
    result = encoder.write_u16_le(0U);
    if (!result) return result.error();
    result = encoder.write_u64_le(record.instance.value());
    if (!result) return result.error();
    result = encoder.write_u64_le(record.identity.process.value());
    if (!result) return result.error();
    result = encoder.write_u64_le(record.identity.principal.high);
    if (!result) return result.error();
    result = encoder.write_u64_le(record.identity.principal.low);
    if (!result) return result.error();
    result = encoder.write_u64_le(record.identity.user.value());
    if (!result) return result.error();
    result = encoder.write_u64_le(record.package_generation);
    if (!result) return result.error();

    for (const auto service : services) {
        result = encoder.write_u32_le(service.value());
        if (!result) return result.error();
        result = encoder.write_u32_le(0U);
        if (!result) return result.error();
    }
    return encoder.written().size();
}

struct DecodedPayloadV2 final {
    ApplicationBootstrapRecordV1 record {};
    std::array<os::core::ServiceId, max_application_service_endpoints_v2> services {};
    std::uint16_t service_count {0U};
};

[[nodiscard]] os::core::Result<DecodedPayloadV2>
decode_payload_v2(os::core::ByteSpan payload) noexcept {
    if (payload.size() < application_bootstrap_payload_base_size_v2 ||
        payload.size() > application_bootstrap_payload_max_size_v2) {
        return app_error(os::core::errors::service::invalid_bootstrap);
    }

    os::ipc::Decoder decoder{payload};
    auto version = decoder.read_u16_le();
    if (!version) return version.error();
    auto size = decoder.read_u16_le();
    if (!size) return size.error();
    auto service_count = decoder.read_u16_le();
    if (!service_count) return service_count.error();
    auto reserved = decoder.read_u16_le();
    if (!reserved) return reserved.error();
    auto instance = decoder.read_u64_le();
    if (!instance) return instance.error();
    auto process = decoder.read_u64_le();
    if (!process) return process.error();
    auto principal_high = decoder.read_u64_le();
    if (!principal_high) return principal_high.error();
    auto principal_low = decoder.read_u64_le();
    if (!principal_low) return principal_low.error();
    auto user = decoder.read_u64_le();
    if (!user) return user.error();
    auto generation = decoder.read_u64_le();
    if (!generation) return generation.error();

    if (version.value() != application_bootstrap_version_v2 || reserved.value() != 0U ||
        service_count.value() == 0U ||
        service_count.value() > max_application_service_endpoints_v2) {
        return app_error(os::core::errors::service::invalid_bootstrap);
    }
    const std::size_t expected_size = application_bootstrap_payload_base_size_v2 +
        (static_cast<std::size_t>(service_count.value()) * application_bootstrap_service_entry_size_v2);
    if (size.value() != expected_size || payload.size() != expected_size) {
        return app_error(os::core::errors::service::invalid_bootstrap);
    }

    DecodedPayloadV2 decoded{
        .record = ApplicationBootstrapRecordV1{
            .instance = os::core::ApplicationInstanceId{instance.value()},
            .identity = os::core::PeerIdentity{
                .principal = os::core::PrincipalId{principal_high.value(), principal_low.value()},
                .user = os::core::UserId{user.value()},
                .process = os::core::ProcessId{process.value()},
            },
            .package_generation = generation.value(),
        },
        .services = {},
        .service_count = service_count.value(),
    };
    if (!valid_record(decoded.record)) {
        return app_error(os::core::errors::service::invalid_bootstrap);
    }

    for (std::size_t index = 0U; index < decoded.service_count; ++index) {
        auto service = decoder.read_u32_le();
        if (!service) return service.error();
        auto entry_reserved = decoder.read_u32_le();
        if (!entry_reserved) return entry_reserved.error();
        if (service.value() == 0U || entry_reserved.value() != 0U) {
            return app_error(os::core::errors::service::invalid_bootstrap);
        }
        decoded.services[index] = os::core::ServiceId{service.value()};
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (decoded.services[prior] == decoded.services[index]) {
                return app_error(os::core::errors::service::invalid_bootstrap);
            }
        }
    }
    auto end = decoder.require_end();
    if (!end) return app_error(os::core::errors::service::invalid_bootstrap);
    return decoded;
}

} // namespace

os::core::Result<os::core::NativeHandle>
ApplicationBootstrapRequestV2::take_service_endpoint(os::core::ServiceId service) noexcept {
    if (service.value() == 0U) return app_error(os::core::errors::service::invalid_bootstrap);
    for (std::size_t index = 0U; index < service_count; ++index) {
        if (services[index] != service) continue;
        if (!endpoints[index].valid()) return ipc_error(os::ipc::errors::invalid_native_handle);
        return std::move(endpoints[index]);
    }
    return app_error(os::core::errors::service::not_supported);
}

os::core::Result<void>
send_bootstrap_request_v2(
    os::ipc::Channel& channel,
    const ApplicationBootstrapRecordV1& record,
    std::span<const os::core::ServiceId> services,
    std::span<const os::core::NativeHandle> endpoints) noexcept {
    if (services.size() != endpoints.size() || !valid_services(services)) {
        return app_error(os::core::errors::service::invalid_bootstrap);
    }
    for (const auto& endpoint : endpoints) {
        if (!endpoint.valid()) return ipc_error(os::ipc::errors::invalid_native_handle);
    }

    std::array<std::byte, application_bootstrap_payload_max_size_v2> payload{};
    auto encoded = encode_payload_v2(payload, record, services);
    if (!encoded) return encoded.error();

    const auto handle_count = static_cast<std::uint16_t>(endpoints.size());
    const os::ipc::WireHeaderV1 header{
        .flags = os::ipc::flag_value(os::ipc::WireFlag::request) |
            os::ipc::flag_value(os::ipc::WireFlag::has_handles),
        .service_id = application_bootstrap_service_id,
        .operation_id = application_bootstrap_operation_initialize_v2,
        .request_id = bootstrap_request_id_v2,
        .payload_size = static_cast<std::uint32_t>(encoded.value()),
        .handle_count = handle_count,
        .payload_checksum = 0U,
    };
    return channel.send(header, {payload.data(), encoded.value()}, endpoints);
}

os::core::Result<ApplicationBootstrapRequestV2>
receive_bootstrap_request_v2(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan receive_buffer) noexcept {
    auto receive = channel.receive(receive_buffer);
    if (!receive) return receive.error();
    auto message = std::move(receive).value();
    const auto& header = message.header();

    const bool valid_header = has_flag(header.flags, os::ipc::WireFlag::request) &&
        has_flag(header.flags, os::ipc::WireFlag::has_handles) &&
        !has_flag(header.flags, os::ipc::WireFlag::response) &&
        !has_flag(header.flags, os::ipc::WireFlag::event) &&
        !has_flag(header.flags, os::ipc::WireFlag::error) &&
        header.service_id == application_bootstrap_service_id &&
        header.operation_id == application_bootstrap_operation_initialize_v2 &&
        header.request_id == bootstrap_request_id_v2 &&
        header.handle_count > 0U &&
        header.handle_count <= max_application_service_endpoints_v2 &&
        message.handle_count() == header.handle_count;
    if (!valid_header) return app_error(os::core::errors::service::invalid_bootstrap);

    auto decoded = decode_payload_v2(message.payload());
    if (!decoded || decoded.value().service_count != header.handle_count) {
        return app_error(os::core::errors::service::invalid_bootstrap);
    }

    ApplicationBootstrapRequestV2 request{};
    request.request_header = header;
    request.record = decoded.value().record;
    request.services = decoded.value().services;
    request.service_count = decoded.value().service_count;
    for (std::uint16_t index = 0U; index < request.service_count; ++index) {
        auto endpoint = message.take_handle(index);
        if (!endpoint) return endpoint.error();
        request.endpoints[index] = std::move(endpoint).value();
    }
    return request;
}

os::core::Result<void>
send_ready_v2(
    os::ipc::Channel& channel,
    const os::ipc::WireHeaderV1& request_header,
    const ApplicationBootstrapRecordV1& record,
    std::span<const os::core::ServiceId> services) noexcept {
    if (request_header.service_id != application_bootstrap_service_id ||
        request_header.operation_id != application_bootstrap_operation_initialize_v2 ||
        request_header.request_id != bootstrap_request_id_v2) {
        return app_error(os::core::errors::service::invalid_bootstrap);
    }
    std::array<std::byte, application_bootstrap_payload_max_size_v2> payload{};
    auto encoded = encode_payload_v2(payload, record, services);
    if (!encoded) return encoded.error();
    return os::ipc::send_rpc_response(
        channel,
        request_header,
        {payload.data(), encoded.value()});
}

os::core::Result<void>
wait_for_ready_v2(
    os::ipc::Channel& channel,
    os::core::MutableByteSpan receive_buffer,
    const ApplicationBootstrapRecordV1& expected,
    std::span<const os::core::ServiceId> expected_services,
    std::uint32_t timeout_ms) noexcept {
    if (!valid_record(expected) || !valid_services(expected_services)) {
        return app_error(os::core::errors::service::invalid_bootstrap);
    }

    pollfd descriptor{
        .fd = channel.native_fd(),
        .events = POLLIN,
        .revents = 0,
    };
    int poll_result = -1;
    do {
        poll_result = ::poll(&descriptor, 1, static_cast<int>(timeout_ms));
    } while (poll_result < 0 && errno == EINTR);
    if (poll_result == 0) return app_error(os::core::errors::service::readiness_timeout);
    if (poll_result < 0) return app_error(os::core::errors::service::launch_failed);

    auto receive = channel.receive(receive_buffer);
    if (!receive) return receive.error();
    auto message = std::move(receive).value();
    const auto& header = message.header();
    const bool valid_header = has_flag(header.flags, os::ipc::WireFlag::response) &&
        !has_flag(header.flags, os::ipc::WireFlag::error) &&
        !has_flag(header.flags, os::ipc::WireFlag::has_handles) &&
        header.service_id == application_bootstrap_service_id &&
        header.operation_id == application_bootstrap_operation_initialize_v2 &&
        header.request_id == bootstrap_request_id_v2 &&
        header.handle_count == 0U && message.handle_count() == 0U;
    if (!valid_header) return app_error(os::core::errors::service::invalid_bootstrap);

    auto decoded = decode_payload_v2(message.payload());
    if (!decoded || decoded.value().record != expected ||
        decoded.value().service_count != expected_services.size()) {
        return app_error(os::core::errors::service::invalid_bootstrap);
    }
    for (std::size_t index = 0U; index < expected_services.size(); ++index) {
        if (decoded.value().services[index] != expected_services[index]) {
            return app_error(os::core::errors::service::invalid_bootstrap);
        }
    }
    return {};
}

} // namespace os::app
