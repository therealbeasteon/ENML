#pragma once

#include <os/ui/text_command_raster.hpp>

namespace os::ui::platform {

// Renderer-private Linux font assets. These paths are platform configuration,
// never application ABI. A product image may point several semantic roles at
// the same coordinated font asset while keeping the semantic roles distinct.
struct LinuxFontFiles final {
    const char* interface_path {nullptr};
    const char* display_path {nullptr};
    const char* international_path {nullptr};
    const char* symbols_path {nullptr};
    const char* monospace_path {nullptr};
};

// Optional production-oriented Linux adapter for the existing bounded ENML
// text seams. It owns FreeType/HarfBuzz state but exposes only ENML callback
// contracts to the renderer. No font path, FT_Face, hb_font_t, glyph bitmap or
// other native object crosses into application-facing semantic APIs.
//
// This backend deliberately does not claim paragraph bidi/line breaking yet;
// that remains a separate renderer-private ParagraphShaper seam so ENML does
// not substitute a partial home-grown Unicode algorithm for a reviewed backend.
class LinuxTextBackend final {
public:
    explicit LinuxTextBackend(const LinuxFontFiles& files) noexcept;
    ~LinuxTextBackend();

    LinuxTextBackend(const LinuxTextBackend&) = delete;
    LinuxTextBackend& operator=(const LinuxTextBackend&) = delete;
    LinuxTextBackend(LinuxTextBackend&&) = delete;
    LinuxTextBackend& operator=(LinuxTextBackend&&) = delete;

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] FontProviderBackend font_provider() noexcept;
    [[nodiscard]] FontAwareTextShaperBackend text_shaper() noexcept;
    [[nodiscard]] FontLineMetricsBackend line_metrics() noexcept;
    [[nodiscard]] GlyphMaskProviderBackend glyph_masks() noexcept;

private:
    struct Impl;
    Impl* impl_ {nullptr};
};

} // namespace os::ui::platform
