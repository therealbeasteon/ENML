#include <cstddef>

extern "C" void* memset(void* destination, int value, std::size_t count) noexcept {
    auto* out = static_cast<unsigned char*>(destination);
    const auto byte = static_cast<unsigned char>(value);
    for (std::size_t i = 0U; i < count; ++i) out[i] = byte;
    return destination;
}

extern "C" void* memcpy(void* destination, const void* source, std::size_t count) noexcept {
    auto* out = static_cast<unsigned char*>(destination);
    const auto* in = static_cast<const unsigned char*>(source);
    for (std::size_t i = 0U; i < count; ++i) out[i] = in[i];
    return destination;
}

extern "C" void* memmove(void* destination, const void* source, std::size_t count) noexcept {
    auto* out = static_cast<unsigned char*>(destination);
    const auto* in = static_cast<const unsigned char*>(source);
    if (out == in || count == 0U) return destination;
    if (out < in) {
        for (std::size_t i = 0U; i < count; ++i) out[i] = in[i];
    } else {
        for (std::size_t i = count; i != 0U; --i) out[i - 1U] = in[i - 1U];
    }
    return destination;
}

extern "C" int memcmp(const void* left, const void* right, std::size_t count) noexcept {
    const auto* a = static_cast<const unsigned char*>(left);
    const auto* b = static_cast<const unsigned char*>(right);
    for (std::size_t i = 0U; i < count; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

// Supplied for the same reason as the four above: the compiler and the standard
// library headers assume these exist, and a -nostdlib image has no libc to take
// them from.
//
// This one arrives less obviously than memcpy. Nothing in the boot code calls
// strlen; std::string_view does. Constructing one from a string literal goes
// through std::char_traits<char>::length, which lowers to a strlen call, so a
// single uart_write("...") pulls in a libc symbol that is not there. The link
// error names char_traits rather than anything written here, which is what makes
// it worth a comment.
//
// Bounded only by the terminator, because that is what the contract is. Callers
// inside the kernel pass literals; anything that ever passes attacker-influenced
// bytes must bound them before they reach a C string, not hope this function
// does it.
extern "C" std::size_t strlen(const char* text) noexcept {
    std::size_t length = 0U;
    while (text[length] != '\0') ++length;
    return length;
}
