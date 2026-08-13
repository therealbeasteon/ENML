#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/storage/protected_format.hpp>
#include <os/storage/protected_publication.hpp>

namespace os::storage {

struct ProtectedObjectVersion final {
    os::core::UserId user {};
    ProtectedObjectId object_id {};
    std::uint64_t generation {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return user.value() != 0U && object_id.valid() && generation != 0U;
    }
};

class ProtectedPublicationBackend {
public:
    virtual ~ProtectedPublicationBackend() = default;

    [[nodiscard]] virtual os::core::Result<void>
    persist_wrapped_key(const ProtectedObjectVersion& next) noexcept = 0;

    [[nodiscard]] virtual os::core::Result<void>
    persist_ciphertext(
        const ProtectedObjectVersion& next,
        os::core::ByteSpan plaintext) noexcept = 0;

    [[nodiscard]] virtual os::core::Result<void>
    persist_commit_record(
        const ProtectedObjectVersion& previous,
        const ProtectedObjectVersion& next) noexcept = 0;

    [[nodiscard]] virtual os::core::Result<void>
    publish_namespace(const ProtectedObjectVersion& next) noexcept = 0;

    [[nodiscard]] virtual os::core::Result<void>
    retire_generation(const ProtectedObjectVersion& previous) noexcept = 0;
};

// Drives the durable publication protocol for one full-object replacement.
// The backend owns filesystem/provider details; this coordinator makes the
// security-critical ordering non-optional and shared by host and Cookie-kernel
// storage implementations.
class ProtectedAtomicReplace final {
public:
    explicit ProtectedAtomicReplace(ProtectedPublicationBackend& backend) noexcept
        : backend_(&backend) {}

    [[nodiscard]] os::core::Result<void>
    replace(
        const ProtectedObjectVersion& previous,
        const ProtectedObjectVersion& next,
        os::core::ByteSpan plaintext) noexcept;

private:
    ProtectedPublicationBackend* backend_ {nullptr};
};

} // namespace os::storage
