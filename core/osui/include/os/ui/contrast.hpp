#pragma once

#include <cstdint>

#include <os/core/result.hpp>
#include <os/ui/raster.hpp>

// Text contrast, as a number the system can refuse on.
//
// ENML already carries a `high_contrast` preference and honours it when
// resolving a visual style. What it has had no way to do is answer the only
// question that matters at the point of use: is *this* text, on *this*
// background, actually readable. A preference expresses an intent; a ratio is a
// fact, and a theme, a token override or a badly chosen accent can produce
// unreadable text with the preference set exactly as the user asked.
//
// The threshold is the widely used accessibility one - 4.5:1 for body text,
// relaxed to 3:1 for large text, where the larger glyph strokes carry the
// contrast the ratio otherwise has to. Both appear in the interface guidance in
// the references.
//
// Why this sits next to the decision-layout rules rather than in an
// accessibility corner: unreadable text on a surface that grants authority is a
// way of not being read. It belongs with the undersized target and the hairline
// refuse - three ways to obtain an answer the user did not knowingly give, none
// of which requires an exploit and none of which any boundary underneath will
// notice, because the user really did press the button.
namespace os::ui {

// Contrast ratios are conventionally written x:1 with one decimal place. They
// are carried here multiplied by ten, as integers, so that a threshold is an
// exact comparison rather than a floating-point one.
inline constexpr std::uint32_t contrast_ratio_scale = 10U;

// 4.5:1 and 3:1.
inline constexpr std::uint32_t minimum_text_contrast_ratio = 45U;
inline constexpr std::uint32_t minimum_large_text_contrast_ratio = 30U;

// The point size at or above which text counts as large, and so may use the
// relaxed threshold. Bold text qualifies at a smaller size; that distinction is
// the caller's to make, since this header does not know the weight.
inline constexpr std::uint32_t large_text_points = 18U;
inline constexpr std::uint32_t large_bold_text_points = 14U;

// Relative luminance of an opaque colour, scaled by one million.
//
// Computed from a fixed table of linearised channel values rather than with
// floating point. Two reasons, and the second is the one that matters here:
// the result is bit-identical on every target ENML builds for, so a contrast
// gate cannot pass on x86 and fail on AArch64 through rounding; and there is no
// dependency on the math library in a path that may run inside the trusted
// shell.
//
// The table is indexed by a colour channel, which is not secret, so this does
// not conflict with the prohibition on secret-indexed lookups in AGENTS.md.
[[nodiscard]] std::uint32_t relative_luminance(Rgba8 color) noexcept;

// Contrast ratio between two opaque colours, scaled by contrast_ratio_scale.
//
// Symmetric: the order of the arguments does not matter, since the ratio is
// defined with the lighter colour on top. Ranges from 10 (identical colours) to
// 210 (black against white).
[[nodiscard]] std::uint32_t contrast_ratio(Rgba8 first, Rgba8 second) noexcept;

// Whether text of the given size is readable against its background.
//
// Both colours must be opaque. A translucent foreground has no defined contrast
// until it is composited, and guessing at the result - by assuming what is
// behind it, or by ignoring alpha - would produce a number that looks like a
// measurement and is not one. Callers must composite first and ask about the
// result.
[[nodiscard]] os::core::Result<void> validate_text_contrast(
    Rgba8 foreground,
    Rgba8 background,
    std::uint32_t point_size,
    bool bold) noexcept;

} // namespace os::ui
