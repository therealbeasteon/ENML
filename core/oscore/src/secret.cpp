#include <os/core/secret.hpp>

namespace os::core {
namespace {

// Prevents the optimiser from reasoning about a value it can otherwise see
// through. Without this the difference accumulation below can be rewritten
// into a short-circuiting comparison, which is exactly the shape being
// avoided, and the zeroing loop can be deleted as a dead store.
inline void optimisation_barrier(unsigned char& value) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" : "+r"(value) : : "memory");
#else
    // No portable equivalent. Volatile is weaker than the barrier above but is
    // the strongest thing the language alone offers.
    volatile unsigned char sink = value;
    value = sink;
#endif
}

inline void memory_barrier() noexcept {
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" : : : "memory");
#endif
}

} // namespace

bool constant_time_equal(ByteSpan left, ByteSpan right) noexcept {
    // A length mismatch is answered immediately and deliberately. The lengths
    // are not secret, and pretending otherwise would mean reading past one of
    // the buffers to keep the timing uniform.
    if (left.size() != right.size()) {
        return false;
    }

    unsigned char difference = 0U;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        difference = static_cast<unsigned char>(
            difference |
            std::to_integer<unsigned char>(left[index] ^ right[index]));
    }

    optimisation_barrier(difference);
    return difference == 0U;
}

void secure_zero(MutableByteSpan bytes) noexcept {
    // Written through a volatile pointer so each store is an observable side
    // effect the compiler may not elide, and followed by a barrier so the whole
    // loop cannot be sunk or removed as dead.
    volatile std::byte* cursor = bytes.data();
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        cursor[index] = std::byte{0};
    }
    memory_barrier();
}

} // namespace os::core
