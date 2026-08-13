#pragma once

#include <os/core/identity.hpp>
#include <os/core/result.hpp>
#include <os/core/span.hpp>
#include <os/storage/path.hpp>

namespace os::storage {

// Service-level seam for the encrypted publication engine. The public Storage
// RPC resolves trusted caller identity and capability rights first, then passes
// that trusted identity plus the confined relative path into this interface.
// Implementations own namespace lookup, key restore/generation, AEAD sealing,
// durable commit ordering and namespace publication.
class ProtectedReplaceHandler {
public:
    virtual ~ProtectedReplaceHandler() = default;

    [[nodiscard]] virtual os::core::Result<void>
    atomic_replace(
        os::core::PrincipalId principal,
        os::core::UserId user,
        const RelativePath& path,
        os::core::ByteSpan contents) noexcept = 0;
};

} // namespace os::storage
