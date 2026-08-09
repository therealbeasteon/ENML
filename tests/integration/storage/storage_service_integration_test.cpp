#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <utility>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/core/error.hpp>
#include <os/core/identity.hpp>
#include <os/core/native_handle.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/decoder.hpp>
#include <os/ipc/encoder.hpp>
#include <os/ipc/rpc.hpp>
#include <os/ipc/wire.hpp>
#include <os/storage/error.hpp>
#include <os/storage/path.hpp>
#include <os/storage/private_root.hpp>
#include <os/storage/service.hpp>

#include "storage_codec.generated.hpp"
#include "storage_types.generated.hpp"

namespace generated = os::services::storage;

namespace {

constexpr os::core::PeerIdentity test_identity{
    .principal = os::core::PrincipalId{0xA102030405060708ULL, 0xB102030405060708ULL},
    .user = os::core::UserId{9U},
    .process = os::core::ProcessId{71U},
};

class TestIdentityResolver final : public os::ipc::PeerIdentityResolver {
public:
    explicit TestIdentityResolver(pid_t client_pid) noexcept
        : client_pid_(client_pid) {}

    os::core::Result<os::core::PeerIdentity>
    resolve(os::ipc::KernelPeerCredentials credentials) noexcept override {
        if (credentials.process_id != static_cast<std::int64_t>(client_pid_) ||
            credentials.user_id != static_cast<std::uint32_t>(::getuid()) ||
            credentials.group_id != static_cast<std::uint32_t>(::getgid())) {
            return os::core::make_error(os::core::ErrorDomain::security, 1U);
        }
        return test_identity;
    }

private:
    pid_t client_pid_ {-1};
};

[[noreturn]] void run_server(
    os::ipc::Channel channel,
    os::storage::PrivateRoot root,
    pid_t client_pid) {
    TestIdentityResolver resolver{client_pid};
    os::storage::PrivateRootRegistry roots;
    auto registered = roots.register_root(
        test_identity.principal,
        test_identity.user,
        std::move(root));
    if (!registered) std::_Exit(20);

    os::storage::StorageService service{channel, resolver, roots};
    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer{};

    for (;;) {
        auto result = service.dispatch_once(receive_buffer, -1);
        if (!result) {
            if (result.error().domain == os::core::ErrorDomain::ipc &&
                result.error().code == os::ipc::errors::peer_died) {
                std::_Exit(0);
            }
            std::_Exit(21);
        }
    }
}

[[nodiscard]] os::ipc::Channel raw_open_root(
    os::ipc::ClientConnection& connection,
    std::array<std::byte, os::ipc::max_wire_packet_size>& response_buffer) {
    generated::OpenPrivateRootRequest request{};
    std::array<std::byte, 16U> request_storage{};
    os::ipc::Encoder encoder{request_storage};
    auto encoded = generated::encode_open_private_root_request(encoder, request);
    assert(encoded);

    auto call = connection.call(
        os::storage::storage_service_id,
        os::storage::storage_open_private_root_operation,
        encoder.written(),
        response_buffer);
    assert(call);
    auto message = std::move(call).value();
    assert(message.handle_count() == 1U);

    os::ipc::Decoder decoder{message.payload()};
    auto response = generated::decode_open_private_root_response(decoder);
    assert(response);
    assert(response.value().object_type ==
           static_cast<std::uint32_t>(os::storage::StorageObjectType::directory));
    assert(response.value().rights == os::storage::directory_rights::all);

    auto native = message.take_handle(0U);
    assert(native);
    auto channel = os::ipc::Channel::adopt(std::move(native).value());
    assert(channel);
    return std::move(channel).value();
}

[[nodiscard]] os::ipc::InboundMessage raw_directory_open(
    os::ipc::Channel& parent,
    const os::storage::RelativePath& path,
    os::storage::RightsMask requested_rights,
    std::uint64_t request_id,
    std::array<std::byte, os::ipc::max_wire_packet_size>& response_buffer) {
    std::array<std::byte, 2048U> payload_storage{};
    os::ipc::Encoder encoder{payload_storage};
    auto result = encoder.write_utf8(path.view(), os::storage::max_relative_path_bytes);
    assert(result);
    result = encoder.write_u32_le(requested_rights);
    assert(result);

    const os::ipc::WireHeaderV1 header{
        .flags = os::ipc::flag_value(os::ipc::WireFlag::request),
        .service_id = os::storage::storage_object_service_id,
        .operation_id = 2U,
        .request_id = os::core::RequestId{request_id},
        .payload_size = static_cast<std::uint32_t>(encoder.written().size()),
        .handle_count = 0U,
        .payload_checksum = 0U,
    };
    auto sent = parent.send(header, encoder.written());
    assert(sent);
    auto received = parent.receive(response_buffer);
    assert(received);
    return std::move(received).value();
}

[[nodiscard]] os::core::Error decode_remote_error(const os::ipc::InboundMessage& message) {
    assert((message.header().flags & os::ipc::flag_value(os::ipc::WireFlag::error)) != 0U);
    assert(message.handle_count() == 0U);
    os::core::Error error{};
    auto decoded = os::ipc::decode_rpc_error(message.payload(), error);
    assert(decoded);
    return error;
}

} // namespace

int main() {
    char root_template[] = "/tmp/emnl-storage-service-XXXXXX";
    char* root_path = ::mkdtemp(root_template);
    assert(root_path != nullptr);

    const int root_fd = ::open(root_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(root_fd >= 0);
    auto root_result = os::storage::PrivateRoot::adopt_authorized_directory(
        os::core::NativeHandle{root_fd});
    assert(root_result);
    auto private_root = std::move(root_result).value();

    auto pair_result = os::ipc::Channel::create_local_pair();
    assert(pair_result);
    auto channels = std::move(pair_result).value();

    const pid_t parent_pid = ::getpid();
    const pid_t child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        channels[0].close();
        run_server(std::move(channels[1]), std::move(private_root), parent_pid);
    }

    channels[1].close();
    os::ipc::ClientConnection connection{channels[0]};
    os::storage::StorageClient storage_client{connection};
    std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

    auto root_handle_result = storage_client.open_private_root(scratch);
    assert(root_handle_result);
    auto root_handle = std::move(root_handle_result).value();
    assert(root_handle.valid());
    assert(root_handle.rights() == os::storage::directory_rights::all);

    auto docs_path = os::storage::RelativePath::parse("docs");
    auto nested_path = os::storage::RelativePath::parse("docs/nested");
    auto note_path = os::storage::RelativePath::parse("docs/note.txt");
    assert(docs_path && nested_path && note_path);

    auto created = root_handle.create_directory(docs_path.value(), scratch);
    assert(created);
    created = root_handle.create_directory(nested_path.value(), scratch);
    assert(created);

    constexpr std::array<std::byte, 5U> hello{
        std::byte{0x68}, std::byte{0x65}, std::byte{0x6c}, std::byte{0x6c}, std::byte{0x6f}
    };
    auto replaced = root_handle.atomic_replace(note_path.value(), hello, scratch);
    assert(replaced);

    os::storage::OpenOptions read_options{
        .access = os::storage::FileAccess::read_only,
        .create = os::storage::CreateDisposition::open_existing,
        .truncate = false,
    };
    auto file_result = root_handle.open_file(note_path.value(), read_options, scratch);
    assert(file_result);
    auto file = std::move(file_result).value();
    assert(file.valid());
    assert((file.rights() & os::storage::file_rights::read) != 0U);
    assert((file.rights() & os::storage::file_rights::write) == 0U);

    auto size_result = file.size(scratch);
    assert(size_result);
    assert(size_result.value() == hello.size());

    std::array<std::byte, 5U> output{};
    auto read_result = file.read_at(0U, output, scratch);
    assert(read_result);
    assert(read_result.value() == hello.size());
    assert(output == hello);

    const auto restricted_rights = os::storage::directory_rights::open_file_read |
        os::storage::directory_rights::open_directory;
    auto docs_handle_result = root_handle.open_directory(
        docs_path.value(), restricted_rights, scratch);
    assert(docs_handle_result);
    auto docs_handle = std::move(docs_handle_result).value();
    assert(docs_handle.rights() == restricted_rights);

    auto denied_create = docs_handle.create_directory(
        os::storage::RelativePath::parse("blocked").value(), scratch);
    assert(!denied_create);
    assert(denied_create.error().domain == os::core::ErrorDomain::storage);
    assert(denied_create.error().code == os::storage::errors::access_denied);

    // Exercise the server-side reduction check directly. The first child
    // capability is intentionally limited to open_directory only. A raw
    // request then tries to delegate the parent's full rights from that child.
    auto raw_root = raw_open_root(connection, scratch);
    auto first_response = raw_directory_open(
        raw_root,
        docs_path.value(),
        os::storage::directory_rights::open_directory,
        1U,
        scratch);
    assert((first_response.header().flags &
            os::ipc::flag_value(os::ipc::WireFlag::error)) == 0U);
    assert(first_response.handle_count() == 1U);

    auto first_native = first_response.take_handle(0U);
    assert(first_native);
    auto restricted_channel_result = os::ipc::Channel::adopt(std::move(first_native).value());
    assert(restricted_channel_result);
    auto restricted_channel = std::move(restricted_channel_result).value();

    auto nested_relative = os::storage::RelativePath::parse("nested");
    assert(nested_relative);
    auto escalation_response = raw_directory_open(
        restricted_channel,
        nested_relative.value(),
        os::storage::directory_rights::all,
        1U,
        scratch);
    auto escalation_error = decode_remote_error(escalation_response);
    assert(escalation_error.domain == os::core::ErrorDomain::storage);
    assert(escalation_error.code == os::storage::errors::invalid_rights);

    restricted_channel.close();
    raw_root.close();
    docs_handle = os::storage::DirectoryObjectHandle{};
    file = os::storage::FileObjectHandle{};
    root_handle = os::storage::DirectoryObjectHandle{};
    channels[0].close();

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);

    char note_native[512]{};
    char nested_native[512]{};
    char docs_native[512]{};
    const int note_length = std::snprintf(
        note_native, sizeof(note_native), "%s/docs/note.txt", root_path);
    const int nested_length = std::snprintf(
        nested_native, sizeof(nested_native), "%s/docs/nested", root_path);
    const int docs_length = std::snprintf(
        docs_native, sizeof(docs_native), "%s/docs", root_path);
    assert(note_length > 0 && static_cast<std::size_t>(note_length) < sizeof(note_native));
    assert(nested_length > 0 && static_cast<std::size_t>(nested_length) < sizeof(nested_native));
    assert(docs_length > 0 && static_cast<std::size_t>(docs_length) < sizeof(docs_native));

    assert(::unlink(note_native) == 0);
    assert(::rmdir(nested_native) == 0);
    assert(::rmdir(docs_native) == 0);
    assert(::rmdir(root_path) == 0);
    return 0;
}
