#include <os/ui/text.hpp>

#include <cstddef>

#include <os/ui/error.hpp>

namespace os::ui {

bool FontFallbackChain::contains(FontFamilyRole role) const noexcept {
    for (std::size_t index = 0U; index < count; ++index) {
        if (families[index] == role) return true;
    }
    return false;
}

os::core::Result<FontFallbackChain> font_fallback_chain(
    TypographyRole role) noexcept {
    FontFallbackChain chain {};

    switch (role) {
    case TypographyRole::body:
    case TypographyRole::label:
        chain.families[0] = FontFamilyRole::interface;
        chain.families[1] = FontFamilyRole::international;
        chain.families[2] = FontFamilyRole::symbols;
        chain.count = 3U;
        return chain;
    case TypographyRole::title:
    case TypographyRole::headline:
        // ENML can have a distinctive platform-owned display face while still
        // falling back to the readable interface/international system stack.
        chain.families[0] = FontFamilyRole::display;
        chain.families[1] = FontFamilyRole::interface;
        chain.families[2] = FontFamilyRole::international;
        chain.families[3] = FontFamilyRole::symbols;
        chain.count = 4U;
        return chain;
    }

    return ui_error(errors::invalid_style);
}

os::core::Result<ResolvedTextStyle> resolve_text_style(
    TypographyRole role,
    std::uint16_t scale_percent) noexcept {
    auto metrics = typography_metrics(role, scale_percent);
    if (!metrics) return metrics.error();

    auto fallback = font_fallback_chain(role);
    if (!fallback) return fallback.error();

    return ResolvedTextStyle{
        .metrics = metrics.value(),
        .fallback = fallback.value(),
    };
}

} // namespace os::ui
