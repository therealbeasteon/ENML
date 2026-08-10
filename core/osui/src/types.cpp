#include <os/ui/types.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <os/ui/error.hpp>

namespace os::ui {
namespace {

[[nodiscard]] constexpr bool continuation(std::uint8_t value) noexcept {
    return (value & 0xC0U) == 0x80U;
}

[[nodiscard]] bool valid_utf8(std::string_view text) noexcept {
    std::size_t index = 0U;
    while (index < text.size()) {
        const auto b0 = static_cast<std::uint8_t>(
            static_cast<unsigned char>(text[index]));
        if (b0 <= 0x7FU) {
            ++index;
            continue;
        }

        if (b0 >= 0xC2U && b0 <= 0xDFU) {
            if (index + 1U >= text.size()) return false;
            const auto b1 = static_cast<std::uint8_t>(
                static_cast<unsigned char>(text[index + 1U]));
            if (!continuation(b1)) return false;
            index += 2U;
            continue;
        }

        if (b0 >= 0xE0U && b0 <= 0xEFU) {
            if (index + 2U >= text.size()) return false;
            const auto b1 = static_cast<std::uint8_t>(
                static_cast<unsigned char>(text[index + 1U]));
            const auto b2 = static_cast<std::uint8_t>(
                static_cast<unsigned char>(text[index + 2U]));
            if (!continuation(b1) || !continuation(b2)) return false;
            if (b0 == 0xE0U && b1 < 0xA0U) return false;
            if (b0 == 0xEDU && b1 >= 0xA0U) return false;
            index += 3U;
            continue;
        }

        if (b0 >= 0xF0U && b0 <= 0xF4U) {
            if (index + 3U >= text.size()) return false;
            const auto b1 = static_cast<std::uint8_t>(
                static_cast<unsigned char>(text[index + 1U]));
            const auto b2 = static_cast<std::uint8_t>(
                static_cast<unsigned char>(text[index + 2U]));
            const auto b3 = static_cast<std::uint8_t>(
                static_cast<unsigned char>(text[index + 3U]));
            if (!continuation(b1) || !continuation(b2) || !continuation(b3)) return false;
            if (b0 == 0xF0U && b1 < 0x90U) return false;
            if (b0 == 0xF4U && b1 > 0x8FU) return false;
            index += 4U;
            continue;
        }

        return false;
    }
    return true;
}

} // namespace

bool semantic_text_valid(const SemanticText& text) noexcept {
    if (static_cast<std::size_t>(text.length) > text.bytes.size()) return false;
    return valid_utf8(text.view());
}

os::core::Result<SemanticText> make_semantic_text(std::string_view text) noexcept {
    if (text.size() > max_semantic_text_bytes) {
        return ui_error(errors::text_too_long);
    }
    if (!valid_utf8(text)) return ui_error(errors::invalid_text);

    SemanticText result{};
    if (!text.empty()) {
        std::memcpy(result.bytes.data(), text.data(), text.size());
    }
    result.length = static_cast<std::uint16_t>(text.size());
    return result;
}

} // namespace os::ui
