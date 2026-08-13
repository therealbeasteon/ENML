#include <os/app/shell_lifecycle_client.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <os/core/error.hpp>
#include <os/ipc/decoder.hpp>
#include <os/package/package.hpp>

namespace os::app {
namespace {

[[nodiscard]] constexpr os::core::Error protocol_error() noexcept {
    return os::core::make_error(
        os::core::ErrorDomain::ipc,
        os::ipc::errors::protocol_violation);
}

[[nodiscard]] os::core::Result<os::core::PeerIdentity> decode_identity(
    os::ipc::Decoder& decoder) noexcept {
    auto high = decoder.read_u64_le();
    if (!high) return high.error();
    auto low = decoder.read_u64_le();
    if (!low) return low.error();
    auto user = decoder.read_u64_le();
    if (!user) return user.error();
    auto process = decoder.read_u64_le();
    if (!process) return process.error();

    const os::core::PeerIdentity identity{
        .principal = {high.value(), low.value()},
        .user = os::core::UserId{user.value()},
        .process = os::core::ProcessId{process.value()},
    };
    if (!os::core::valid_peer_identity(identity)) return protocol_error();
    return identity;
}

[[nodiscard]] os::core::Result<os::package::ApplicationIdentity> decode_application(
    os::ipc::Decoder& decoder) noexcept {
    auto package_text = decoder.read_utf8(os::package::max_package_id_bytes);
    if (!package_text) return package_text.error();
    auto package_id = os::package::PackageId::parse(package_text.value());
    if (!package_id) return protocol_error();

    auto signer_bytes = decoder.read_raw(os::package::signer_lineage_id_bytes);
    if (!signer_bytes) return signer_bytes.error();
    os::package::SignerLineageId signer{};
    std::copy(
        signer_bytes.value().begin(),
        signer_bytes.value().end(),
        signer.bytes.begin());
    if (!signer.valid()) return protocol_error();

    return os::package::ApplicationIdentity{
        .package_id = package_id.value(),
        .signer_lineage = signer,
    };
}

[[nodiscard]] bool snapshot_valid(const ApplicationLifecycleSnapshot& snapshot) noexcept {
    if (snapshot.revision == 0U || snapshot.count > snapshot.applications.size()) return false;

    std::uint64_t previous_instance = 0U;
    for (std::size_t index = 0U; index < snapshot.count; ++index) {
        const auto& record = snapshot.applications[index];
        if (!record.valid() || record.instance.value() <= previous_instance) return false;
        previous_instance = record.instance.value();

        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            const auto& previous = snapshot.applications[earlier];
            if (previous.instance == record.instance || previous.identity == record.identity) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] os::core::Result<ApplicationLifecycleSnapshot> decode_snapshot(
    os::core::ByteSpan payload) noexcept {
    os::ipc::Decoder decoder{payload};
    auto revision = decoder.read_u64_le();
    if (!revision) return revision.error();
    auto count = decoder.read_u16_le();
    if (!count) return count.error();
    if (revision.value() == 0U || count.value() > max_application_lifecycle_instances) {
        return protocol_error();
    }

    ApplicationLifecycleSnapshot snapshot{};
    snapshot.revision = revision.value();
    snapshot.count = count.value();
    for (std::size_t index = 0U; index < snapshot.count; ++index) {
        auto instance = decoder.read_u64_le();
        if (!instance) return instance.error();
        auto application = decode_application(decoder);
        if (!application) return application.error();
        auto identity = decode_identity(decoder);
        if (!identity) return identity.error();
        snapshot.applications[index] = ApplicationLifecycleRecord{
            .instance = os::core::ApplicationInstanceId{instance.value()},
            .application = application.value(),
            .identity = identity.value(),
        };
    }
    auto end = decoder.require_end();
    if (!end || !snapshot_valid(snapshot)) return protocol_error();
    return snapshot;
}

} // namespace

os::core::Result<ApplicationLifecycleSnapshot> ShellLifecycleControlClient::snapshot(
    os::core::MutableByteSpan scratch) noexcept {
    auto response = connection_.call(
        shell_lifecycle_control_service_id,
        shell_lifecycle_operation_snapshot,
        {},
        scratch);
    if (!response) return response.error();
    if (response.value().handle_count() != 0U) return protocol_error();
    return decode_snapshot(response.value().payload());
}

os::core::Result<os::ipc::Channel>
ShellLifecycleControlClient::take_compositor_capability(
    os::core::MutableByteSpan scratch) noexcept {
    auto response = connection_.call(
        shell_lifecycle_control_service_id,
        shell_lifecycle_operation_take_compositor,
        {},
        scratch);
    if (!response) return response.error();

    auto message = std::move(response).value();
    if (!message.payload().empty() || message.handle_count() != 1U) {
        return protocol_error();
    }
    auto handle = message.take_handle(0U);
    if (!handle) return handle.error();
    auto channel = os::ipc::Channel::adopt(std::move(handle).value());
    if (!channel) return channel.error();
    return std::move(channel).value();
}

} // namespace os::app
