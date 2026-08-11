#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

#include <os/core/native_handle.hpp>
#include <os/keys/persistence.hpp>
#include <os/keys/registry.hpp>
#include <os/keys/testing/openssl_provider.hpp>

// The KRG1 durable key registry parser.
//
// This is the one parser in the tree that consumes *durable state* rather than
// message bytes: it reads what survived a reboot. An attacker with write access
// to the state directory - the exact thing an offline attack yields - chooses
// its entire input, and whatever it produces then becomes the key registry the
// system trusts.
//
// Why it is driven through the filesystem rather than a byte span:
// PersistentKeyRegistry::load_snapshot interleaves decoding with read_exact
// calls on the state directory descriptor, so there is no seam to hand a
// buffer. The alternative was separating decode from I/O, but that means
// modifying a durable key-state substrate AGENTS.md explicitly guards, purely
// for testability. Refactoring security-critical persistence to make it easier
// to test is the wrong trade when a slower harness reaches the same code.
//
// The cost is throughput: one write and one open per execution instead of a
// pure in-memory call. It is paid once per input rather than per byte, and the
// staging directory is created once rather than per execution, which keeps this
// within range of useful for a structured format this small.

namespace {

constexpr const char* registry_file_name = "key-registry-v1.bin";

char staging_directory[64] = {};
int staging_fd = -1;

[[nodiscard]] bool ensure_staging() noexcept {
    if (staging_fd >= 0) {
        return true;
    }
    std::snprintf(staging_directory, sizeof(staging_directory), "/tmp/emnl-krg1-XXXXXX");
    if (::mkdtemp(staging_directory) == nullptr) {
        return false;
    }
    staging_fd = ::open(staging_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    return staging_fd >= 0;
}

[[nodiscard]] bool write_snapshot(const std::uint8_t* data, std::size_t size) noexcept {
    const int fd = ::openat(
        staging_fd, registry_file_name, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        return false;
    }

    std::size_t written = 0U;
    while (written < size) {
        const auto amount = ::write(fd, data + written, size - written);
        if (amount < 0) {
            if (errno == EINTR) {
                continue;
            }
            ::close(fd);
            return false;
        }
        written += static_cast<std::size_t>(amount);
    }
    ::close(fd);
    return true;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (!ensure_staging() || !write_snapshot(data, size)) {
        return 0;
    }

    // open() takes ownership of the descriptor, so each execution hands it a
    // duplicate and the staging directory stays open for the next one.
    const int duplicated = ::dup(staging_fd);
    if (duplicated < 0) {
        return 0;
    }

    // A fresh provider per execution. Carrying provider state across inputs
    // would make a finding depend on everything the fuzzer happened to try
    // before it, which is the opposite of a reproducer.
    os::keys::testing::OpenSslTestKeyProvider provider;

    auto opened = os::keys::PersistentKeyRegistry::open(
        os::core::NativeHandle{duplicated}, provider);
    if (!opened) {
        return 0;
    }
    const auto& registry = opened.value();

    // Anything accepted must be internally coherent. A snapshot that decodes
    // into a registry claiming more records than the table holds, or more live
    // records than records, would mean attacker-chosen durable bytes had
    // steered the registry into a state the rest of the key service assumes is
    // impossible.
    if (registry.record_count() > os::keys::max_key_records) {
        __builtin_trap();
    }
    if (registry.active_count() > registry.record_count()) {
        __builtin_trap();
    }

    return 0;
}
