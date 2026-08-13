// M4.10u: the protected replacement seam, exercised through the public RPC.
//
// M4.10t added ProtectedReplaceHandler and a StorageService member to hold one,
// and nothing ever called it. This test fixes the two properties that make the
// seam worth having, and both are about what the handler is *told* rather than
// about encryption:
//
//   1. Identity is server-held. The handler receives the PrincipalId and UserId
//      fixed when the capability was minted, never a value from the request.
//   2. The path is the canonical root-relative address. A child directory
//      capability writing "note2.txt" must reach the handler as
//      "docs/note2.txt", or a confined capability could publish over a
//      root-level object of the same name.
//
// The handler reports failure by exiting with a distinct status so a mismatch
// cannot be mistaken for a passing run, matching run_server's existing style.

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <os/core/error.hpp>
#include <os/core/identity.hpp>
#include <os/core/native_handle.hpp>
#include <os/ipc/constants.hpp>
#include <os/ipc/rpc.hpp>
#include <os/storage/error.hpp>
#include <os/storage/path.hpp>
#include <os/storage/private_root.hpp>
#include <os/storage/protected_service.hpp>
#include <os/storage/service.hpp>

namespace {

constexpr os::core::PeerIdentity test_identity{
    .principal = os::core::PrincipalId{0xA102030405060708ULL, 0xB102030405060708ULL},
    .user = os::core::UserId{9U},
    .process = os::core::ProcessId{71U},
};

constexpr std::array<std::byte, 5U> hello{
    std::byte{0x68}, std::byte{0x65}, std::byte{0x6c}, std::byte{0x6c}, std::byte{0x6f}
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

// Records what the service hands down and refuses anything unexpected. It
// deliberately writes nothing durable: the parent then proves the substrate
// path was not also taken by checking that no file appeared on disk.
class RecordingProtectedReplace final : public os::storage::ProtectedReplaceHandler {
public:
    os::core::Result<void>
    atomic_replace(
        os::core::PrincipalId principal,
        os::core::UserId user,
        const os::storage::RelativePath& path,
        os::core::ByteSpan contents) noexcept override {
        if (principal != test_identity.principal) std::_Exit(30);
        if (user != test_identity.user) std::_Exit(31);
        if (contents.size() != hello.size()) std::_Exit(32);

        // First call comes from the private-root capability, which has no
        // namespace address of its own, so the path arrives unchanged. The
        // second comes from the "docs" child capability and must have been
        // composed.
        const std::string_view expected =
            calls_ == 0U ? std::string_view{"docs/note.txt"}
                         : std::string_view{"docs/note2.txt"};
        if (path.view() != expected) std::_Exit(33);

        ++calls_;
        return {};
    }

    [[nodiscard]] std::size_t calls() const noexcept { return calls_; }

private:
    std::size_t calls_ {0U};
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

    RecordingProtectedReplace protected_replace;
    os::storage::StorageService service{channel, resolver, roots, &protected_replace};
    std::array<std::byte, os::ipc::max_wire_packet_size> receive_buffer{};

    for (;;) {
        auto result = service.dispatch_once(receive_buffer, -1);
        if (!result) {
            if (result.error().domain == os::core::ErrorDomain::ipc &&
                result.error().code == os::ipc::errors::peer_died) {
                // Both replacements must have reached the handler. Exiting 0
                // on a handler that was never called would make this test
                // pass against the very defect it exists to catch.
                if (protected_replace.calls() != 2U) std::_Exit(34);
                std::_Exit(0);
            }
            std::_Exit(21);
        }
    }
}

[[nodiscard]] bool exists_at(const char* root_path, const char* relative) {
    const int root_fd = ::open(root_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(root_fd >= 0);
    struct stat info{};
    const bool found = ::fstatat(root_fd, relative, &info, AT_SYMLINK_NOFOLLOW) == 0;
    ::close(root_fd);
    return found;
}

} // namespace

int main() {
    char root_template[] = "/tmp/emnl-storage-protected-XXXXXX";
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
    {
        os::ipc::ClientConnection connection{channels[0]};
        os::storage::StorageClient storage_client{connection};
        std::array<std::byte, os::ipc::max_wire_packet_size> scratch{};

        auto root_handle_result = storage_client.open_private_root(scratch);
        assert(root_handle_result);
        auto root_handle = std::move(root_handle_result).value();
        assert(root_handle.valid());

        auto docs_path = os::storage::RelativePath::parse("docs");
        auto note_path = os::storage::RelativePath::parse("docs/note.txt");
        auto note2_path = os::storage::RelativePath::parse("note2.txt");
        assert(docs_path && note_path && note2_path);

        // create_directory is not part of the seam, so this still reaches the
        // substrate and gives the child capability something to open.
        auto created = root_handle.create_directory(docs_path.value(), scratch);
        assert(created);

        // From the root capability: path passes through unchanged.
        auto replaced = root_handle.atomic_replace(note_path.value(), hello, scratch);
        assert(replaced);

        // From a child capability: the server must compose "docs" + "note2.txt"
        // before the handler ever sees it.
        const auto docs_rights = os::storage::directory_rights::atomic_replace |
            os::storage::directory_rights::open_file_read;
        auto docs_handle_result = root_handle.open_directory(
            docs_path.value(), docs_rights, scratch);
        assert(docs_handle_result);
        auto docs_handle = std::move(docs_handle_result).value();

        auto child_replaced = docs_handle.atomic_replace(note2_path.value(), hello, scratch);
        assert(child_replaced);
    }

    // Closing the connection ends the server loop, which checks the call count.
    channels[0].close();

    int status = 0;
    assert(::waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);

    // The handler wrote nothing, so neither object may exist. If the substrate
    // path had also run, these would be on disk and the seam would be a
    // duplicate write rather than a redirection.
    assert(!exists_at(root_path, "docs/note.txt"));
    assert(!exists_at(root_path, "docs/note2.txt"));
    assert(exists_at(root_path, "docs"));

    return 0;
}
