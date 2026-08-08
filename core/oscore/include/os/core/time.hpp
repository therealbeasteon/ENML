#pragma once

#include <chrono>
#include <cstdint>

namespace os::core {

class Duration final {
public:
    constexpr Duration() noexcept = default;

    static constexpr Duration from_nanoseconds(std::int64_t value) noexcept {
        return Duration{value};
    }

    [[nodiscard]] constexpr std::int64_t nanoseconds() const noexcept { return nanoseconds_; }

private:
    explicit constexpr Duration(std::int64_t value) noexcept : nanoseconds_(value) {}
    std::int64_t nanoseconds_ {0};
};

class MonotonicTime final {
public:
    constexpr MonotonicTime() noexcept = default;

    static constexpr MonotonicTime from_nanoseconds(std::int64_t value) noexcept {
        return MonotonicTime{value};
    }

    [[nodiscard]] constexpr std::int64_t nanoseconds() const noexcept { return nanoseconds_; }

private:
    explicit constexpr MonotonicTime(std::int64_t value) noexcept : nanoseconds_(value) {}
    std::int64_t nanoseconds_ {0};
};

} // namespace os::core
