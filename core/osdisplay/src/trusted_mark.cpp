#include <os/display/trusted_mark.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <os/display/error.hpp>

namespace os::display {
namespace {

[[nodiscard]] bool target_valid(const TrustedMarkRasterTarget& target) noexcept {
    if (target.pixels == nullptr || target.width == 0U || target.height == 0U ||
        target.width > max_display_dimension_px || target.height > max_display_dimension_px ||
        target.stride < target.width || target.stride > max_display_dimension_px) {
        return false;
    }
    const std::size_t required =
        static_cast<std::size_t>(target.stride) * static_cast<std::size_t>(target.height);
    return target.pixel_count >= required;
}

[[nodiscard]] constexpr bool opaque(TrustedMarkRgba8 color) noexcept {
    return color.alpha == 255U;
}

[[nodiscard]] constexpr bool theme_valid(const TrustedMarkTheme& theme) noexcept {
    return opaque(theme.foundation) && opaque(theme.system_chrome) &&
        opaque(theme.secure_system) &&
        theme.foundation != theme.system_chrome &&
        theme.foundation != theme.secure_system &&
        theme.system_chrome != theme.secure_system;
}

[[nodiscard]] constexpr bool presentation_valid(TrustedPresentation presentation) noexcept {
    return presentation == TrustedPresentation::system_chrome ||
        presentation == TrustedPresentation::secure_system;
}

[[nodiscard]] bool entry_valid(
    const TrustedOverlayEntry& entry,
    const TrustedMarkRasterTarget& target) noexcept {
    if (!valid_display_object_value(entry.surface.value()) ||
        !presentation_valid(entry.presentation) || !entry.bounds.nonempty() ||
        entry.frame_sequence == 0U || entry.bounds.x < 0 || entry.bounds.y < 0) {
        return false;
    }
    const std::uint64_t right =
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(entry.bounds.x)) +
        static_cast<std::uint64_t>(entry.bounds.width);
    const std::uint64_t bottom =
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(entry.bounds.y)) +
        static_cast<std::uint64_t>(entry.bounds.height);
    return right <= target.width && bottom <= target.height;
}

[[nodiscard]] std::size_t pixel_index(
    const TrustedMarkRasterTarget& target,
    std::uint32_t x,
    std::uint32_t y) noexcept {
    return static_cast<std::size_t>(y) * target.stride + static_cast<std::size_t>(x);
}

void write_pixel(
    TrustedMarkRasterTarget target,
    std::uint32_t x,
    std::uint32_t y,
    TrustedMarkRgba8 color,
    TrustedMarkRasterStats& stats) noexcept {
    target.pixels[pixel_index(target, x, y)] = color;
    ++stats.pixel_writes;
}

void fill_rect(
    TrustedMarkRasterTarget target,
    std::uint32_t left,
    std::uint32_t top,
    std::uint32_t width,
    std::uint32_t height,
    TrustedMarkRgba8 color,
    TrustedMarkRasterStats& stats) noexcept {
    const std::uint32_t right = left + width;
    const std::uint32_t bottom = top + height;
    for (std::uint32_t y = top; y < bottom; ++y) {
        for (std::uint32_t x = left; x < right; ++x) {
            write_pixel(target, x, y, color, stats);
        }
    }
}

void draw_diagonal(
    TrustedMarkRasterTarget target,
    std::uint32_t left,
    std::uint32_t top,
    std::uint32_t length,
    std::uint32_t thickness,
    TrustedMarkRgba8 color,
    TrustedMarkRasterStats& stats) noexcept {
    if (length == 0U || thickness == 0U) return;
    // A shallow rising seam gives secure-system attribution a visibly distinct
    // asymmetric signature without borrowing a lock/shield glyph. Integer-only
    // placement is deterministic across architecture and has O(length*thickness)
    // bounded cost.
    for (std::uint32_t step = 0U; step < length; ++step) {
        const std::uint32_t x = left + step;
        const std::uint32_t y = top + (length - 1U - step) / 2U;
        for (std::uint32_t offset = 0U; offset < thickness; ++offset) {
            if (y + offset < target.height) {
                write_pixel(target, x, y + offset, color, stats);
            }
        }
    }
}

void draw_mark(
    const TrustedOverlayEntry& entry,
    const TrustedMarkTheme& theme,
    TrustedMarkRasterTarget target,
    TrustedMarkRasterStats& stats) noexcept {
    const std::uint32_t available = std::min(entry.bounds.width, entry.bounds.height);
    if (available == 0U) return;

    const bool secure = entry.presentation == TrustedPresentation::secure_system;
    const std::uint32_t preferred_length = secure ? 18U : 14U;
    const std::uint32_t length = std::min(available, preferred_length);
    const std::uint32_t foundation_thickness =
        length >= 10U ? 3U : (length >= 5U ? 2U : 1U);
    const std::uint32_t core_thickness = length >= 7U ? 2U : 1U;
    const std::uint32_t right =
        static_cast<std::uint32_t>(entry.bounds.x) + entry.bounds.width;
    const std::uint32_t top = static_cast<std::uint32_t>(entry.bounds.y);
    const std::uint32_t left = right - length;
    const TrustedMarkRgba8 core = secure ? theme.secure_system : theme.system_chrome;

    // Opaque contrast cradle. Because this pass runs after client composition,
    // its legibility does not depend on sampling or trusting client pixels.
    fill_rect(
        target,
        left,
        top,
        length,
        foundation_thickness,
        theme.foundation,
        stats);
    fill_rect(
        target,
        right - foundation_thickness,
        top,
        foundation_thickness,
        length,
        theme.foundation,
        stats);

    const std::uint32_t core_top =
        top + (foundation_thickness > core_thickness
            ? (foundation_thickness - core_thickness) / 2U
            : 0U);
    const std::uint32_t core_right = right - core_thickness;
    fill_rect(target, left, core_top, length, core_thickness, core, stats);
    fill_rect(target, core_right, top, core_thickness, length, core, stats);

    if (secure && length >= 4U) {
        // Secure-system surfaces add a second asymmetric seam. Paint a wider
        // foundation stroke first and a one-pixel core stroke second so the
        // signature remains visible over both light and dark trusted content.
        const std::uint32_t diagonal_length = length - 2U;
        const std::uint32_t diagonal_left = left + 1U;
        const std::uint32_t diagonal_top = top + foundation_thickness;
        draw_diagonal(
            target,
            diagonal_left,
            diagonal_top,
            diagonal_length,
            2U,
            theme.foundation,
            stats);
        draw_diagonal(
            target,
            diagonal_left,
            diagonal_top,
            diagonal_length,
            1U,
            core,
            stats);
    }

    ++stats.marks_drawn;
}

} // namespace

os::core::Result<TrustedMarkRasterStats> rasterize_trusted_marks(
    const TrustedOverlaySnapshot& overlay,
    const TrustedMarkTheme& theme,
    TrustedMarkRasterTarget target) noexcept {
    if (!target_valid(target)) return display_error(errors::invalid_trusted_mark_target);
    if (!theme_valid(theme)) return display_error(errors::invalid_trusted_mark_theme);
    if (overlay.count > overlay.entries.size()) {
        return display_error(errors::invalid_trusted_overlay);
    }

    TrustedMarkRasterStats stats{};
    for (std::size_t index = 0U; index < overlay.count; ++index) {
        const TrustedOverlayEntry& entry = overlay.entries[index];
        ++stats.entries_seen;
        if (!entry_valid(entry, target)) {
            return display_error(errors::invalid_trusted_overlay);
        }
        draw_mark(entry, theme, target, stats);
    }
    return stats;
}

} // namespace os::display
