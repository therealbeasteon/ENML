#pragma once

#include <cstddef>
#include <span>

namespace os::core {

template <typename T>
using Span = std::span<T>;

using ByteSpan = std::span<const std::byte>;
using MutableByteSpan = std::span<std::byte>;

} // namespace os::core
