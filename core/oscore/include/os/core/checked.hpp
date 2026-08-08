#pragma once

#include <cstddef>
#include <limits>

#include <os/core/error.hpp>
#include <os/core/result.hpp>

namespace os::core {

[[nodiscard]] inline Result<std::size_t>
checked_add(std::size_t a, std::size_t b) noexcept {
    if (b > std::numeric_limits<std::size_t>::max() - a) {
        return core_error(errors::core::overflow);
    }
    return a + b;
}

[[nodiscard]] inline Result<std::size_t>
checked_mul(std::size_t a, std::size_t b) noexcept {
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) {
        return core_error(errors::core::overflow);
    }
    return a * b;
}

[[nodiscard]] constexpr bool
range_fits(std::size_t total, std::size_t offset, std::size_t size) noexcept {
    return offset <= total && size <= total - offset;
}

} // namespace os::core
