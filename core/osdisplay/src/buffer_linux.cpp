#include <os/display/buffer.hpp>

#include <climits>
#include <cstdint>
#include <limits>

#include <fcntl.h>
#include <linux/memfd.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <os/display/error.hpp>

namespace os::display {
namespace {

inline constexpr std::uint64_t bytes_per_pixel = 4U;

[[nodiscard]] os::core::Result<os::core::NativeHandle>
create_backing_memory(std::uint64_t byte_size) noexcept {
    unsigned int flags = MFD_CLOEXEC | MFD_ALLOW_SEALING;
#ifdef MFD_NOEXEC_SEAL
    flags |= MFD_NOEXEC_SEAL;
#endif

    long native = ::syscall(SYS_memfd_create, "enml-display-buffer", flags);
#ifdef MFD_NOEXEC_SEAL
    if (native < 0 && errno == EINVAL) {
        flags &= ~static_cast<unsigned int>(MFD_NOEXEC_SEAL);
        native = ::syscall(SYS_memfd_create, "enml-display-buffer", flags);
    }
#endif
    if (native < 0 || native > INT_MAX) {
        return display_error(errors::buffer_create_failed);
    }

    os::core::NativeHandle memory{static_cast<int>(native)};
    if (::ftruncate(memory.native(), static_cast<off_t>(byte_size)) != 0) {
        return display_error(errors::buffer_create_failed);
    }

    const int seals = F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL;
    if (::fcntl(memory.native(), F_ADD_SEALS, seals) != 0) {
        return display_error(errors::buffer_create_failed);
    }
    return memory;
}

[[nodiscard]] os::core::Result<os::core::NativeHandle>
duplicate_handle(const os::core::NativeHandle& source) noexcept {
    if (!source.valid()) return display_error(errors::invalid_buffer);
    const int duplicate = ::fcntl(source.native(), F_DUPFD_CLOEXEC, 0);
    if (duplicate < 0) return display_error(errors::buffer_create_failed);
    return os::core::NativeHandle{duplicate};
}

} // namespace

bool SharedBufferPool::valid_format(PixelFormat format) noexcept {
    switch (format) {
    case PixelFormat::rgba8888:
    case PixelFormat::rgbx8888:
        return true;
    }
    return false;
}

os::core::Result<BufferDescriptor> SharedBufferPool::make_descriptor(
    BufferId id,
    os::core::PeerIdentity owner,
    PixelSize size,
    PixelFormat format) noexcept {
    if (!valid_display_object_value(id.value()) || !os::core::valid_peer_identity(owner)) {
        return display_error(errors::invalid_identity);
    }
    if (!size.valid()) return display_error(errors::invalid_geometry);
    if (!valid_format(format)) return display_error(errors::invalid_pixel_format);

    const std::uint64_t stride = static_cast<std::uint64_t>(size.width) * bytes_per_pixel;
    if (stride == 0U || stride > std::numeric_limits<std::uint32_t>::max()) {
        return display_error(errors::buffer_bytes_limit);
    }
    const std::uint64_t bytes = stride * static_cast<std::uint64_t>(size.height);
    if (bytes == 0U || bytes > max_shared_buffer_bytes) {
        return display_error(errors::buffer_bytes_limit);
    }

    return BufferDescriptor{
        .id = id,
        .owner = owner,
        .size = size,
        .format = format,
        .stride_bytes = static_cast<std::uint32_t>(stride),
        .byte_size = bytes,
    };
}

os::core::Result<SharedBufferLease> SharedBufferPool::allocate(
    os::core::PeerIdentity owner,
    PixelSize size,
    PixelFormat format) noexcept {
    if (!valid()) return display_error(errors::invalid_configuration);
    if (!os::core::valid_peer_identity(owner)) return display_error(errors::invalid_identity);
    if (next_buffer_serial_ == 0U) return display_error(errors::buffer_id_exhausted);
    if (buffer_count_ >= max_shared_buffers) return display_error(errors::buffer_limit);
    if (buffer_count_for(owner.principal) >= max_shared_buffers_per_principal) {
        return display_error(errors::principal_buffer_limit);
    }

    const std::uint64_t id_value = make_display_object_value(object_generation_, next_buffer_serial_);
    if (id_value == 0U) return display_error(errors::buffer_id_exhausted);
    auto descriptor_result = make_descriptor(BufferId{id_value}, owner, size, format);
    if (!descriptor_result) return descriptor_result.error();
    const BufferDescriptor descriptor = descriptor_result.value();

    const std::uint64_t principal_bytes = byte_count_for(owner.principal);
    if (principal_bytes > max_shared_buffer_bytes_per_principal - descriptor.byte_size ||
        byte_count_ > max_shared_buffer_bytes_global - descriptor.byte_size) {
        return display_error(errors::buffer_bytes_limit);
    }

    Entry* free_entry = nullptr;
    for (auto& entry : entries_) {
        if (!entry.occupied) {
            free_entry = &entry;
            break;
        }
    }
    if (free_entry == nullptr) return display_error(errors::buffer_limit);

    auto memory_result = create_backing_memory(descriptor.byte_size);
    if (!memory_result) return memory_result.error();
    auto memory = std::move(memory_result).value();
    auto duplicate_result = duplicate_handle(memory);
    if (!duplicate_result) return duplicate_result.error();

    free_entry->occupied = true;
    free_entry->descriptor = descriptor;
    free_entry->memory = std::move(memory);
    ++buffer_count_;
    byte_count_ += descriptor.byte_size;

    if (next_buffer_serial_ == std::numeric_limits<std::uint32_t>::max()) {
        next_buffer_serial_ = 0U;
    } else {
        ++next_buffer_serial_;
    }

    return SharedBufferLease{
        .descriptor = descriptor,
        .memory = std::move(duplicate_result).value(),
    };
}

SharedBufferPool::Entry* SharedBufferPool::find(BufferId buffer) noexcept {
    if (buffer.value() == 0U) return nullptr;
    for (auto& entry : entries_) {
        if (entry.occupied && entry.descriptor.id == buffer) return &entry;
    }
    return nullptr;
}

const SharedBufferPool::Entry* SharedBufferPool::find(BufferId buffer) const noexcept {
    if (buffer.value() == 0U) return nullptr;
    for (const auto& entry : entries_) {
        if (entry.occupied && entry.descriptor.id == buffer) return &entry;
    }
    return nullptr;
}

void SharedBufferPool::erase(Entry& entry) noexcept {
    if (!entry.occupied) return;
    if (byte_count_ >= entry.descriptor.byte_size) {
        byte_count_ -= entry.descriptor.byte_size;
    } else {
        byte_count_ = 0U;
    }
    if (buffer_count_ != 0U) --buffer_count_;
    entry = Entry{};
}

os::core::Result<void> SharedBufferPool::release(
    os::core::PeerIdentity caller,
    BufferId buffer) noexcept {
    if (!os::core::valid_peer_identity(caller)) return display_error(errors::invalid_identity);
    Entry* entry = find(buffer);
    if (entry == nullptr) return display_error(errors::invalid_buffer);
    if (entry->descriptor.owner != caller) return display_error(errors::buffer_owner_mismatch);
    erase(*entry);
    return {};
}

os::core::Result<BufferDescriptor> SharedBufferPool::lookup_owned(
    os::core::PeerIdentity caller,
    BufferId buffer) const noexcept {
    if (!os::core::valid_peer_identity(caller)) return display_error(errors::invalid_identity);
    const Entry* entry = find(buffer);
    if (entry == nullptr) return display_error(errors::invalid_buffer);
    if (entry->descriptor.owner != caller) return display_error(errors::buffer_owner_mismatch);
    return entry->descriptor;
}

os::core::Result<os::core::NativeHandle> SharedBufferPool::duplicate_memory(
    os::core::PeerIdentity caller,
    BufferId buffer) const noexcept {
    auto owned = lookup_owned(caller, buffer);
    if (!owned) return owned.error();
    const Entry* entry = find(buffer);
    if (entry == nullptr) return display_error(errors::invalid_buffer);
    return duplicate_handle(entry->memory);
}

void SharedBufferPool::revoke_process(os::core::ProcessId process) noexcept {
    if (process.value() == 0U) return;
    for (auto& entry : entries_) {
        if (entry.occupied && entry.descriptor.owner.process == process) erase(entry);
    }
}

std::size_t SharedBufferPool::buffer_count_for(os::core::PrincipalId principal) const noexcept {
    if (!os::core::valid_principal(principal)) return 0U;
    std::size_t count = 0U;
    for (const auto& entry : entries_) {
        if (entry.occupied && entry.descriptor.owner.principal == principal) ++count;
    }
    return count;
}

std::uint64_t SharedBufferPool::byte_count_for(os::core::PrincipalId principal) const noexcept {
    if (!os::core::valid_principal(principal)) return 0U;
    std::uint64_t bytes = 0U;
    for (const auto& entry : entries_) {
        if (entry.occupied && entry.descriptor.owner.principal == principal) {
            bytes += entry.descriptor.byte_size;
        }
    }
    return bytes;
}

} // namespace os::display
