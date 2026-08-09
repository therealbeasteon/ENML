#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/native_handle.hpp>
#include <os/core/result.hpp>
#include <os/display/types.hpp>

namespace os::display {

// Move-only application-side/shared-memory lease. The semantic BufferId is
// still revalidated by the compositor service on every frame submission; the
// native handle is only the mapped pixel-memory transport object.
struct SharedBufferLease final {
    BufferDescriptor descriptor {};
    os::core::NativeHandle memory {};

    SharedBufferLease() noexcept = default;
    SharedBufferLease(const SharedBufferLease&) = delete;
    SharedBufferLease& operator=(const SharedBufferLease&) = delete;
    SharedBufferLease(SharedBufferLease&&) noexcept = default;
    SharedBufferLease& operator=(SharedBufferLease&&) noexcept = default;

    [[nodiscard]] bool valid() const noexcept {
        return descriptor.valid() && memory.valid();
    }
};

class SharedBufferPool final {
public:
    SharedBufferPool() noexcept = default;
    SharedBufferPool(const SharedBufferPool&) = delete;
    SharedBufferPool& operator=(const SharedBufferPool&) = delete;

    [[nodiscard]] os::core::Result<SharedBufferLease> allocate(
        os::core::PeerIdentity owner,
        PixelSize size,
        PixelFormat format) noexcept;

    [[nodiscard]] os::core::Result<void> release(
        os::core::PeerIdentity caller,
        BufferId buffer) noexcept;

    [[nodiscard]] os::core::Result<BufferDescriptor> lookup_owned(
        os::core::PeerIdentity caller,
        BufferId buffer) const noexcept;

    [[nodiscard]] os::core::Result<os::core::NativeHandle> duplicate_memory(
        os::core::PeerIdentity caller,
        BufferId buffer) const noexcept;

    void revoke_process(os::core::ProcessId process) noexcept;

    [[nodiscard]] std::size_t buffer_count() const noexcept { return buffer_count_; }
    [[nodiscard]] std::size_t buffer_count_for(os::core::PrincipalId principal) const noexcept;
    [[nodiscard]] std::uint64_t byte_count() const noexcept { return byte_count_; }
    [[nodiscard]] std::uint64_t byte_count_for(os::core::PrincipalId principal) const noexcept;

private:
    struct Entry final {
        bool occupied {false};
        BufferDescriptor descriptor {};
        os::core::NativeHandle memory {};
    };

    [[nodiscard]] static bool valid_format(PixelFormat format) noexcept;
    [[nodiscard]] static os::core::Result<BufferDescriptor> make_descriptor(
        BufferId id,
        os::core::PeerIdentity owner,
        PixelSize size,
        PixelFormat format) noexcept;
    [[nodiscard]] Entry* find(BufferId buffer) noexcept;
    [[nodiscard]] const Entry* find(BufferId buffer) const noexcept;
    void erase(Entry& entry) noexcept;

    std::array<Entry, max_shared_buffers> entries_ {};
    std::uint64_t next_buffer_id_ {1U};
    std::size_t buffer_count_ {0U};
    std::uint64_t byte_count_ {0U};
};

} // namespace os::display
