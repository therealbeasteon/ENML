#pragma once

#include <os/core/result.hpp>
#include <os/keys/key.hpp>

namespace os::keys {

// Logical KeyId generation is separate from secret-key generation. KeyId is a
// locator, not authority, but it must not be silently reused. Production may
// use a collision-resistant random source while tests inject deterministic ids.
class KeyIdSource {
public:
    virtual ~KeyIdSource() = default;

    [[nodiscard]] virtual os::core::Result<KeyId> next() noexcept = 0;
};

} // namespace os::keys
