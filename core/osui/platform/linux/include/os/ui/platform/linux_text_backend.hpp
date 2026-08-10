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
// text seams. FreeType owns face loading/rasterization, HarfBuzz owns OpenType
// shaping and ICU owns Unicode bidi/line-break analysis. Native objects remain
// renderer-private: applications still receive only semantic typography roles,
// bounded text and opaque face IDs.
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
    [[nodiscard]] FontAwareParagraphShaperBackend paragraph_shaper() noexcept;
    [[nodiscard]] FontLineMetricsBackend line_metrics() noexcept;
    [[nodiscard]] GlyphMaskProviderBackend glyph_masks() noexcept;

    // Convenience bundle for the existing RenderCommand text path. The bundle
    // contains callback views into this backend and is valid only while this
    // LinuxTextBackend remains alive.
    [[nodiscard]] TextCommandRasterBackend command_backend() noexcept;

private:
    struct Impl;
    Impl* impl_ {nullptr};
};

} // namespace os::ui::platform
