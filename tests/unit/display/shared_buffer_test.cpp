#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include <fcntl.h>
#include <sys/mman.h>

#include <os/display/buffer.hpp>
#include <os/display/error.hpp>

namespace {

constexpr os::core::PrincipalId first_principal{0x4255464645520001ULL, 1U};
constexpr os::core::PrincipalId second_principal{0x4255464645520002ULL, 2U};
constexpr os::core::PeerIdentity first{
    first_principal, os::core::UserId{9U}, os::core::ProcessId{9001U}};
constexpr os::core::PeerIdentity first_new_process{
    first_principal, os::core::UserId{9U}, os::core::ProcessId{9002U}};
constexpr os::core::PeerIdentity second{
    second_principal, os::core::UserId{9U}, os::core::ProcessId{9101U}};

void expect_error(const os::core::Error& error, std::uint32_t code) {
    assert(error.domain == os::core::ErrorDomain::display);
    assert(error.code == code);
}

} // namespace

int main() {
    os::display::SharedBufferPool pool;

    auto allocated = pool.allocate(first, {64U, 32U}, os::display::PixelFormat::rgba8888);
    assert(allocated);
    auto lease = std::move(allocated).value();
    assert(lease.valid());
    assert(lease.descriptor.owner == first);
    assert(lease.descriptor.stride_bytes == 256U);
    assert(lease.descriptor.byte_size == 8192U);
    assert(pool.buffer_count() == 1U);
    assert(pool.byte_count() == 8192U);

    const int seals = ::fcntl(lease.memory.native(), F_GET_SEALS);
    assert(seals >= 0);
    assert((seals & F_SEAL_SHRINK) != 0);
    assert((seals & F_SEAL_GROW) != 0);
    assert((seals & F_SEAL_SEAL) != 0);

    void* writable = ::mmap(
        nullptr,
        static_cast<std::size_t>(lease.descriptor.byte_size),
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        lease.memory.native(),
        0);
    assert(writable != MAP_FAILED);
    auto* bytes = static_cast<std::byte*>(writable);
    bytes[0] = std::byte{0xA5};
    bytes[lease.descriptor.byte_size - 1U] = std::byte{0x5A};

    auto duplicate = pool.duplicate_memory(first, lease.descriptor.id);
    assert(duplicate);
    void* readonly = ::mmap(
        nullptr,
        static_cast<std::size_t>(lease.descriptor.byte_size),
        PROT_READ,
        MAP_SHARED,
        duplicate.value().native(),
        0);
    assert(readonly != MAP_FAILED);
    const auto* observed = static_cast<const std::byte*>(readonly);
    assert(observed[0] == std::byte{0xA5});
    assert(observed[lease.descriptor.byte_size - 1U] == std::byte{0x5A});
    assert(::munmap(readonly, static_cast<std::size_t>(lease.descriptor.byte_size)) == 0);
    assert(::munmap(writable, static_cast<std::size_t>(lease.descriptor.byte_size)) == 0);

    auto stolen = pool.lookup_owned(second, lease.descriptor.id);
    assert(!stolen);
    expect_error(stolen.error(), os::display::errors::buffer_owner_mismatch);
    auto same_principal_new_process = pool.lookup_owned(first_new_process, lease.descriptor.id);
    assert(!same_principal_new_process);
    expect_error(same_principal_new_process.error(), os::display::errors::buffer_owner_mismatch);

    auto too_large = pool.allocate(first, {4096U, 4096U}, os::display::PixelFormat::rgba8888);
    assert(!too_large);
    expect_error(too_large.error(), os::display::errors::buffer_bytes_limit);

    // The per-principal count is independent of global capacity.
    std::array<os::display::SharedBufferLease, os::display::max_shared_buffers_per_principal - 1U> extras{};
    for (auto& extra : extras) {
        auto next = pool.allocate(first, {32U, 32U}, os::display::PixelFormat::rgbx8888);
        assert(next);
        extra = std::move(next).value();
    }
    assert(pool.buffer_count_for(first_principal) == os::display::max_shared_buffers_per_principal);
    auto over_count = pool.allocate(first, {32U, 32U}, os::display::PixelFormat::rgba8888);
    assert(!over_count);
    expect_error(over_count.error(), os::display::errors::principal_buffer_limit);

    // A separate principal still has independent admission budget.
    auto independent = pool.allocate(second, {128U, 64U}, os::display::PixelFormat::rgba8888);
    assert(independent);
    assert(pool.buffer_count_for(second_principal) == 1U);

    // Process revocation closes the pool's retained backing handles. Client
    // duplicates can remain mapped, but their semantic BufferIds are no longer
    // valid for compositor submission.
    pool.revoke_process(first.process);
    assert(pool.buffer_count_for(first_principal) == 0U);
    assert(!pool.lookup_owned(first, lease.descriptor.id));
    assert(pool.lookup_owned(second, independent.value().descriptor.id));

    assert(pool.release(second, independent.value().descriptor.id));
    assert(pool.buffer_count_for(second_principal) == 0U);
    auto double_release = pool.release(second, independent.value().descriptor.id);
    assert(!double_release);
    expect_error(double_release.error(), os::display::errors::invalid_buffer);

    return 0;
}
