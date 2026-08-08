#pragma once

#include <os/core/span.hpp>
#include <os/ipc/wire.hpp>

namespace os::ipc {

struct InboundMessageView final {
    WireHeaderV1 header;
    os::core::ByteSpan payload;
};

} // namespace os::ipc
