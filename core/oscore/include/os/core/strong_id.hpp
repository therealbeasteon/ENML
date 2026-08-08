#pragma once

#include <compare>
#include <concepts>
#include <cstdint>

namespace os::core {

template <typename Tag, std::integral Rep>
class StrongId final {
public:
    using rep_type = Rep;

    constexpr StrongId() noexcept = default;
    explicit constexpr StrongId(Rep value) noexcept : value_(value) {}

    [[nodiscard]] constexpr Rep value() const noexcept { return value_; }

    [[nodiscard]] friend constexpr auto operator<=>(const StrongId&, const StrongId&) = default;

private:
    Rep value_ {};
};

struct ProcessIdTag;
struct ServiceIdTag;
struct RequestIdTag;
struct UserIdTag;
struct ApplicationInstanceIdTag;

using ProcessId = StrongId<ProcessIdTag, std::uint64_t>;
using ServiceId = StrongId<ServiceIdTag, std::uint32_t>;
using RequestId = StrongId<RequestIdTag, std::uint64_t>;
using UserId = StrongId<UserIdTag, std::uint64_t>;
using ApplicationInstanceId = StrongId<ApplicationInstanceIdTag, std::uint64_t>;

struct PrincipalId final {
    std::uint64_t high {0};
    std::uint64_t low {0};

    [[nodiscard]] friend constexpr auto operator<=>(const PrincipalId&, const PrincipalId&) = default;
};

} // namespace os::core
