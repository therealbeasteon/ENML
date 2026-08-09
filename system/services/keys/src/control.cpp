#include <os/keys/control.hpp>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include <poll.h>

#include <os/core/error.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>
#include <os/ipc/rpc.hpp>
#include <os/ipc/wire.hpp>
#include <os/keys/error.hpp>
#include <os/service/bootstrap.hpp>

namespace os::keys {
namespace {

inline constexpr std::size_t identity_payload_size = 48U;
inline constexpr std::size_t owner_payload_size = 24U;
inline constexpr std::size_t profile_payload_size = 8U;

[[nodiscard]] constexpr os::core::Error ipc_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::ipc, code);
}

[[nodiscard]] constexpr os::core::Error service_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::service, code);
}

[[nodiscard]] constexpr os::core::Error security_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::security, code);
}

[[nodiscard]] constexpr bool has_flag(std::uint32_t flags, os::ipc::WireFlag flag) noexcept {
    return (flags & os::ipc::flag_value(flag)) != 0U;
}

[[nodiscard]] os::core::Result<os::service::ProcessIdentityRecord>
decode_identity_record(os::core::ByteSpan payload) noexcept {
    if (payload.size() != identity_payload_size) {
        return security_error(os::core::errors::security::invalid_identity);
    }

    os::ipc::Decoder decoder{payload};
    auto process = decoder.read_u64_le();
    if (!process) return process.error();
    auto principal_high = decoder.read_u64_le();
    if (!principal_high) return principal_high.error();
    auto principal_low = decoder.read_u64_le();
    if (!principal_low) return principal_low.error();
    auto user = decoder.read_u64_le();
    if (!user) return user.error();
    auto native_pid = decoder.read_u64_le();
    if (!native_pid) return native_pid.error();
    auto native_uid = decoder.read_u32_le();
    if (!native_uid) return native_uid.error();
    auto native_gid = decoder.read_u32_le();
    if (!native_gid) return native_gid.error();
    auto end = decoder.require_end();
    if (!end) return end.error();

    if (process.value() == 0U || native_pid.value() == 0U ||
        native_pid.value() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return security_error(os::core::errors::security::invalid_identity);
    }

    const os::service::ProcessIdentityRecord record{
        .kernel = os::ipc::KernelPeerCredentials{
            .process_id = static_cast<std::int64_t>(native_pid.value()),
            .user_id = native_uid.value(),
            .group_id = native_gid.value(),
        },
        .peer = os::core::PeerIdentity{
            .principal = os::core::PrincipalId{
                .high = principal_high.value(),
                .low = principal_low.value(),
            },
            .user = os::core::UserId{user.value()},
            .process = os::core::ProcessId{process.value()},
        },
    };
    if (!os::core::valid_peer_identity(record.peer)) {
        return security_error(os::core::errors::security::invalid_identity);
    }
    return record;
}

[[nodiscard]] os::core::Result<KeyOwner>
decode_owner(os::core::ByteSpan payload) noexcept {
    if (payload.size() != owner_payload_size) {
        return key_error(errors::invalid_key);
    }
    os::ipc::Decoder decoder{payload};
    auto high = decoder.read_u64_le();
    if (!high) return high.error();
    auto low = decoder.read_u64_le();
    if (!low) return low.error();
    auto user = decoder.read_u64_le();
    if (!user) return user.error();
    auto end = decoder.require_end();
    if (!end) return end.error();

    const KeyOwner owner{
        .principal = os::core::PrincipalId{high.value(), low.value()},
        .user = os::core::UserId{user.value()},
    };
    if (!os::core::valid_principal(owner.principal)) {
        return key_error(errors::invalid_key);
    }
    return owner;
}

[[nodiscard]] os::core::Result<os::core::UserId>
decode_profile_user(os::core::ByteSpan payload) noexcept {
    if (payload.size() != profile_payload_size) {
        return key_error(errors::invalid_protection_scope);
    }
    os::ipc::Decoder decoder{payload};
    auto user = decoder.read_u64_le();
    if (!user) return user.error();
    auto end = decoder.require_end();
    if (!end) return end.error();
    return os::core::UserId{user.value()};
}

[[nodiscard]] os::core::Result<void>
encode_owner(
    os::ipc::Encoder& encoder,
    os::core::PrincipalId principal,
    os::core::UserId user) noexcept {
    if (!os::core::valid_principal(principal)) return key_error(errors::invalid_key);
    auto result = encoder.write_u64_le(principal.high);
    if (!result) return result.error();
    result = encoder.write_u64_le(principal.low);
    if (!result) return result.error();
    return encoder.write_u64_le(user.value());
}

[[nodiscard]] os::core::Result<os::core::RequestId>
next_request_id(std::uint64_t& next) noexcept {
    if (next == 0U) return ipc_error(os::ipc::errors::request_id_exhausted);
    const os::core::RequestId request{next};
    if (next == std::numeric_limits<std::uint64_t>::max()) {
        next = 0U;
    } else {
        ++next;
    }
    return request;
}

[[nodiscard]] os::core::Result<void>
wait_for_response(
    os::ipc::Channel& control,
    os::core::RequestId request_id,
    std::uint32_t operation_id,
    os::core::MutableByteSpan scratch,
    std::uint32_t timeout_ms) noexcept {
    if (scratch.size() < os::ipc::max_wire_packet_size) {
        return ipc_error(os::ipc::errors::buffer_too_small);
    }

    pollfd descriptor{.fd = control.native_fd(), .events = POLLIN, .revents = 0};
    int result = -1;
    do {
        result = ::poll(&descriptor, 1, static_cast<int>(timeout_ms));
    } while (result < 0 && errno == EINTR);
    if (result == 0) return service_error(os::core::errors::service::readiness_timeout);
    if (result < 0) return service_error(os::core::errors::service::launch_failed);

    auto received = control.receive(scratch);
    if (!received) return received.error();
    auto response = std::move(received).value();
    const auto& header = response.header();
    if (!has_flag(header.flags, os::ipc::WireFlag::response) ||
        has_flag(header.flags, os::ipc::WireFlag::request) ||
        has_flag(header.flags, os::ipc::WireFlag::event) ||
        has_flag(header.flags, os::ipc::WireFlag::oneway) ||
        has_flag(header.flags, os::ipc::WireFlag::cancellable) ||
        header.service_id != os::service::bootstrap_service_id ||
        header.operation_id != operation_id || header.request_id != request_id ||
        header.handle_count != 0U || response.handle_count() != 0U) {
        return ipc_error(os::ipc::errors::protocol_violation);
    }
    if (has_flag(header.flags, os::ipc::WireFlag::error)) {
        os::core::Error remote{};
        auto decoded = os::ipc::decode_rpc_error(response.payload(), remote);
        if (!decoded) return decoded.error();
        return remote;
    }
    if (!response.payload().empty()) return ipc_error(os::ipc::errors::protocol_violation);
    return {};
}

} // namespace

os::core::Result<void>
KeyControlClient::send_request(
    std::uint32_t operation,
    os::core::ByteSpan payload,
    os::core::MutableByteSpan scratch,
    std::uint32_t timeout_ms) noexcept {
    if (control_ == nullptr || !control_->valid()) {
        return ipc_error(os::ipc::errors::invalid_channel);
    }
    if (payload.size() > os::ipc::max_inline_payload_size ||
        scratch.size() < os::ipc::max_wire_packet_size) {
        return ipc_error(os::ipc::errors::buffer_too_small);
    }
    auto request = next_request_id(next_request_id_);
    if (!request) return request.error();

    const os::ipc::WireHeaderV1 header{
        .flags = os::ipc::flag_value(os::ipc::WireFlag::request),
        .service_id = os::service::bootstrap_service_id,
        .operation_id = operation,
        .request_id = request.value(),
        .payload_size = static_cast<std::uint32_t>(payload.size()),
        .handle_count = 0U,
        .payload_checksum = 0U,
    };
    auto sent = control_->send(header, payload);
    if (!sent) return sent.error();
    return wait_for_response(*control_, request.value(), operation, scratch, timeout_ms);
}

os::core::Result<void>
KeyControlClient::ensure_profile(
    os::core::UserId user,
    os::core::MutableByteSpan scratch,
    std::uint32_t timeout_ms) noexcept {
    std::array<std::byte, profile_payload_size> payload{};
    os::ipc::Encoder encoder{payload};
    auto encoded = encoder.write_u64_le(user.value());
    if (!encoded) return encoded.error();
    return send_request(
        key_control_ensure_profile_operation,
        encoder.written(),
        scratch,
        timeout_ms);
}

os::core::Result<void>
KeyControlClient::enable_application(
    os::core::PrincipalId principal,
    os::core::UserId user,
    os::core::MutableByteSpan scratch,
    std::uint32_t timeout_ms) noexcept {
    std::array<std::byte, owner_payload_size> payload{};
    os::ipc::Encoder encoder{payload};
    auto encoded = encode_owner(encoder, principal, user);
    if (!encoded) return encoded.error();
    return send_request(
        key_control_enable_application_operation,
        encoder.written(),
        scratch,
        timeout_ms);
}

os::core::Result<void>
KeyControlClient::disable_application(
    os::core::PrincipalId principal,
    os::core::UserId user,
    os::core::MutableByteSpan scratch,
    std::uint32_t timeout_ms) noexcept {
    std::array<std::byte, owner_payload_size> payload{};
    os::ipc::Encoder encoder{payload};
    auto encoded = encode_owner(encoder, principal, user);
    if (!encoded) return encoded.error();
    return send_request(
        key_control_disable_application_operation,
        encoder.written(),
        scratch,
        timeout_ms);
}

void KeyService::revoke_owner(KeyOwner owner) noexcept {
    for (std::size_t index = 0U; index < objects_.size(); ++index) {
        if (objects_[index].occupied && objects_[index].owner == owner) {
            clear_slot(index);
        }
    }
}

os::core::Result<void>
KeyControlRouter::dispatch_once(
    os::ipc::Channel& control,
    os::core::MutableByteSpan receive_buffer) noexcept {
    if (policy_ == nullptr || hierarchy_ == nullptr || service_ == nullptr ||
        identities_ == nullptr) {
        return key_error(errors::provider_failure);
    }

    auto received = control.receive(receive_buffer);
    if (!received) return received.error();
    auto message = std::move(received).value();
    const auto& header = message.header();

    const bool base_valid = has_flag(header.flags, os::ipc::WireFlag::request) &&
        !has_flag(header.flags, os::ipc::WireFlag::response) &&
        !has_flag(header.flags, os::ipc::WireFlag::event) &&
        !has_flag(header.flags, os::ipc::WireFlag::error) &&
        !has_flag(header.flags, os::ipc::WireFlag::oneway) &&
        !has_flag(header.flags, os::ipc::WireFlag::cancellable) &&
        header.service_id == os::service::bootstrap_service_id &&
        header.request_id.value() != 0U;
    if (!base_valid) return service_error(os::core::errors::service::invalid_bootstrap);

    os::core::Result<void> operation{};
    if (header.operation_id == os::service::identity_operation_register) {
        if (!has_flag(header.flags, os::ipc::WireFlag::has_handles) ||
            header.handle_count != 1U || message.handle_count() != 1U) {
            operation = security_error(os::core::errors::security::invalid_identity);
        } else {
            auto record = decode_identity_record(message.payload());
            if (!record) {
                operation = record.error();
            } else {
                auto pidfd = message.take_handle(0U);
                if (!pidfd) {
                    operation = pidfd.error();
                } else {
                    operation = identities_->register_process(
                        record.value(), std::move(pidfd).value());
                }
            }
        }
    } else if (header.operation_id == os::service::identity_operation_unregister) {
        if (has_flag(header.flags, os::ipc::WireFlag::has_handles) ||
            header.handle_count != 0U || message.handle_count() != 0U) {
            operation = security_error(os::core::errors::security::invalid_identity);
        } else {
            os::ipc::Decoder decoder{message.payload()};
            auto process = decoder.read_u64_le();
            if (!process) {
                operation = process.error();
            } else {
                auto end = decoder.require_end();
                if (!end || process.value() == 0U) {
                    operation = security_error(os::core::errors::security::invalid_identity);
                } else {
                    operation = identities_->unregister_process(os::core::ProcessId{process.value()});
                }
            }
        }
    } else if (header.operation_id == key_control_ensure_profile_operation) {
        if (has_flag(header.flags, os::ipc::WireFlag::has_handles) ||
            header.handle_count != 0U || message.handle_count() != 0U) {
            operation = key_error(errors::access_denied);
        } else {
            auto user = decode_profile_user(message.payload());
            if (!user) {
                operation = user.error();
            } else {
                operation = hierarchy_->ensure_profile(profile_key_binding(user.value()));
            }
        }
    } else if (header.operation_id == key_control_enable_application_operation) {
        if (has_flag(header.flags, os::ipc::WireFlag::has_handles) ||
            header.handle_count != 0U || message.handle_count() != 0U) {
            operation = key_error(errors::access_denied);
        } else {
            auto owner = decode_owner(message.payload());
            if (!owner) {
                operation = owner.error();
            } else {
                auto profile = hierarchy_->ensure_profile(profile_key_binding(owner.value().user));
                if (!profile) {
                    operation = profile.error();
                } else {
                    auto application = hierarchy_->ensure_application(
                        application_key_binding(owner.value().principal, owner.value().user));
                    if (!application) {
                        operation = application.error();
                    } else {
                        operation = policy_->enable(owner.value());
                    }
                }
            }
        }
    } else if (header.operation_id == key_control_disable_application_operation) {
        if (has_flag(header.flags, os::ipc::WireFlag::has_handles) ||
            header.handle_count != 0U || message.handle_count() != 0U) {
            operation = key_error(errors::access_denied);
        } else {
            auto owner = decode_owner(message.payload());
            if (!owner) {
                operation = owner.error();
            } else {
                operation = policy_->disable(owner.value());
                if (operation) service_->revoke_owner(owner.value());
            }
        }
    } else {
        operation = os::core::make_error(
            os::core::ErrorDomain::service,
            os::core::errors::service::unknown_operation);
    }

    if (!operation) {
        return os::ipc::send_rpc_error(control, header, operation.error());
    }
    return os::ipc::send_rpc_response(control, header, {});
}

} // namespace os::keys
