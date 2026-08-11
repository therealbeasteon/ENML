#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <os/core/secret.hpp>

namespace {

bool check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "secret: %s\n", what);
    }
    return condition;
}

os::core::ByteSpan span_of(const std::array<std::byte, 32>& value) {
    return {value.data(), value.size()};
}

} // namespace

// These tests establish correctness, not constant-timeness. Timing cannot be
// asserted meaningfully on a shared CI runner - the noise floor is far above
// the signal, and a flaky timing gate would be worse than none. What is
// verifiable here is that the comparison is right at every difference position,
// including the last, which is where a short-circuiting implementation and a
// constant-time one behave identically and a wrong loop bound does not.
// Constant-timeness rests on the shape of the implementation and its barrier.
int main() {
    std::array<std::byte, 32> left{};
    std::array<std::byte, 32> right{};
    for (std::size_t index = 0U; index < left.size(); ++index) {
        left[index] = static_cast<std::byte>(index * 7U + 1U);
        right[index] = left[index];
    }

    if (!check(os::core::constant_time_equal(span_of(left), span_of(right)),
               "identical spans compared unequal")) return 1;

    // A difference at any single position must be found, including the final
    // byte, which a loop that stops one short would miss.
    for (std::size_t index = 0U; index < right.size(); ++index) {
        const auto original = right[index];
        right[index] = static_cast<std::byte>(std::to_integer<std::uint8_t>(original) ^ 0x01U);
        if (!check(!os::core::constant_time_equal(span_of(left), span_of(right)),
                   "difference not detected")) return 1;
        // Flipping the high bit as well, in case only low bits are accumulated.
        right[index] = static_cast<std::byte>(std::to_integer<std::uint8_t>(original) ^ 0x80U);
        if (!check(!os::core::constant_time_equal(span_of(left), span_of(right)),
                   "high-bit difference not detected")) return 1;
        right[index] = original;
    }
    if (!check(os::core::constant_time_equal(span_of(left), span_of(right)),
               "restoring the buffer did not restore equality")) return 1;

    // Differing lengths are unequal, and a prefix match does not count.
    if (!check(!os::core::constant_time_equal({left.data(), 31U}, span_of(right)),
               "differing lengths compared equal")) return 1;
    if (!check(!os::core::constant_time_equal(span_of(left), {right.data(), 0U}),
               "empty span matched a non-empty one")) return 1;

    // Two empty spans are equal, and the loop must not read anything.
    if (!check(os::core::constant_time_equal({left.data(), 0U}, {right.data(), 0U}),
               "empty spans compared unequal")) return 1;

    // secure_zero must actually zero, and must not touch what follows it.
    {
        std::array<std::byte, 48> buffer{};
        for (std::size_t index = 0U; index < buffer.size(); ++index) {
            buffer[index] = static_cast<std::byte>(0xA5U);
        }
        os::core::secure_zero({buffer.data(), 32U});
        for (std::size_t index = 0U; index < 32U; ++index) {
            if (!check(buffer[index] == std::byte{0}, "secure_zero left a byte set")) return 1;
        }
        for (std::size_t index = 32U; index < buffer.size(); ++index) {
            if (!check(buffer[index] == std::byte{0xA5U},
                       "secure_zero wrote past the span")) return 1;
        }
        // Zeroing nothing must be safe.
        os::core::secure_zero({buffer.data(), 0U});
        if (!check(buffer[32U] == std::byte{0xA5U}, "empty secure_zero wrote a byte")) return 1;
    }

    return 0;
}
