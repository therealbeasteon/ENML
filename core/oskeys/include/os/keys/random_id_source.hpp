#pragma once

#include <os/keys/id_source.hpp>

namespace os::keys {

// Linux implementation for production logical ids. KeyId is not secret and is
// never authorization, but a collision-resistant random id avoids predictable
// reuse across process/service/device restarts before durable registry state is
// introduced.
class RandomKeyIdSource final : public KeyIdSource {
public:
    [[nodiscard]] os::core::Result<KeyId> next() noexcept override;
};

} // namespace os::keys
