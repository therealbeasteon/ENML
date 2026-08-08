#pragma once

#include <type_traits>

namespace os::core {

template <typename Enum>
requires std::is_enum_v<Enum>
class Flags final {
public:
    using underlying_type = std::underlying_type_t<Enum>;

    constexpr Flags() noexcept = default;
    constexpr Flags(Enum value) noexcept : bits_(static_cast<underlying_type>(value)) {}
    explicit constexpr Flags(underlying_type bits) noexcept : bits_(bits) {}

    [[nodiscard]] constexpr underlying_type bits() const noexcept { return bits_; }

    [[nodiscard]] constexpr bool contains(Enum value) const noexcept {
        const auto mask = static_cast<underlying_type>(value);
        return (bits_ & mask) == mask;
    }

    constexpr Flags& set(Enum value) noexcept {
        bits_ = static_cast<underlying_type>(bits_ | static_cast<underlying_type>(value));
        return *this;
    }

    constexpr Flags& clear(Enum value) noexcept {
        bits_ = static_cast<underlying_type>(bits_ & ~static_cast<underlying_type>(value));
        return *this;
    }

private:
    underlying_type bits_ {0};
};

} // namespace os::core
