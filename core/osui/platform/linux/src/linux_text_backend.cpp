#include <os/ui/platform/linux_text_backend.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include <hb-ft.h>
#include <hb.h>
#include <unicode/ubidi.h>
#include <unicode/ubrk.h>
#include <unicode/utypes.h>

namespace os::ui::platform {
namespace {

inline constexpr std::size_t family_slot_count = 5U;
inline constexpr FT_F26Dot6 default_face_size_26_6 = 16L * 64L;
inline constexpr std::size_t max_utf16_units = max_semantic_text_bytes;
inline constexpr std::size_t max_break_points = max_semantic_text_bytes + 1U;

struct Utf16Text final {
    std::array<UChar, max_utf16_units> units {};
    // A non-negative entry marks a real Unicode scalar boundary and maps the
    // UTF-16 offset back to the original UTF-8 byte offset. The interior of a
    // surrogate pair remains -1 so a malformed backend split cannot be hidden.
    std::array<std::int32_t, max_utf16_units + 1U> byte_boundary {};
    std::int32_t length {0};
};

struct BreakPoint final {
    std::int32_t utf16_offset {0};
    bool hard {false};
};

struct BreakPointSet final {
    std::array<BreakPoint, max_break_points> points {};
    std::size_t count {0U};
};

struct FamilySegment final {
    std::uint16_t byte_start {0U};
    std::uint16_t byte_end {0U};
    FontFamilyRole family {FontFamilyRole::interface};
};

struct FamilySegments final {
    std::array<FamilySegment, max_shaped_runs> segments {};
    std::size_t count {0U};
};

[[nodiscard]] constexpr std::size_t family_index(FontFamilyRole family) noexcept {
    switch (family) {
    case FontFamilyRole::interface: return 0U;
    case FontFamilyRole::display: return 1U;
    case FontFamilyRole::international: return 2U;
    case FontFamilyRole::symbols: return 3U;
    case FontFamilyRole::monospace: return 4U;
    }
    return family_slot_count;
}

[[nodiscard]] constexpr TextDirection text_direction(UBiDiDirection direction) noexcept {
    return direction == UBIDI_RTL ? TextDirection::right_to_left : TextDirection::left_to_right;
}

[[nodiscard]] constexpr hb_direction_t hb_direction(TextDirection direction) noexcept {
    return direction == TextDirection::right_to_left ? HB_DIRECTION_RTL : HB_DIRECTION_LTR;
}

[[nodiscard]] bool decode_utf8_scalar(
    const SemanticText& source,
    std::size_t offset,
    std::uint32_t& scalar,
    std::size_t& width) noexcept {
    if (offset >= static_cast<std::size_t>(source.length)) return false;
    const auto first = static_cast<std::uint8_t>(source.bytes[offset]);
    if (first < 0x80U) {
        scalar = first;
        width = 1U;
        return true;
    }

    if ((first & 0xE0U) == 0xC0U) width = 2U;
    else if ((first & 0xF0U) == 0xE0U) width = 3U;
    else if ((first & 0xF8U) == 0xF0U) width = 4U;
    else return false;
    if (offset + width > static_cast<std::size_t>(source.length)) return false;

    std::uint32_t value = first & (width == 2U ? 0x1FU : width == 3U ? 0x0FU : 0x07U);
    for (std::size_t index = 1U; index < width; ++index) {
        const auto next = static_cast<std::uint8_t>(source.bytes[offset + index]);
        if ((next & 0xC0U) != 0x80U) return false;
        value = (value << 6U) | static_cast<std::uint32_t>(next & 0x3FU);
    }
    scalar = value;
    return true;
}

[[nodiscard]] bool utf16_from_utf8(
    const SemanticText& source,
    Utf16Text& output) noexcept {
    output = {};
    output.byte_boundary.fill(-1);
    output.byte_boundary[0] = 0;

    std::size_t byte_offset = 0U;
    std::size_t utf16_offset = 0U;
    while (byte_offset < static_cast<std::size_t>(source.length)) {
        std::uint32_t scalar = 0U;
        std::size_t width = 0U;
        if (!decode_utf8_scalar(source, byte_offset, scalar, width) || scalar > 0x10FFFFU ||
            (scalar >= 0xD800U && scalar <= 0xDFFFU)) {
            return false;
        }

        output.byte_boundary[utf16_offset] = static_cast<std::int32_t>(byte_offset);
        if (scalar <= 0xFFFFU) {
            if (utf16_offset >= output.units.size()) return false;
            output.units[utf16_offset] = static_cast<UChar>(scalar);
            ++utf16_offset;
        } else {
            if (utf16_offset + 2U > output.units.size()) return false;
            const std::uint32_t adjusted = scalar - 0x10000U;
            output.units[utf16_offset] = static_cast<UChar>(0xD800U + (adjusted >> 10U));
            output.units[utf16_offset + 1U] = static_cast<UChar>(
                0xDC00U + (adjusted & 0x3FFU));
            ++utf16_offset;
            // The offset between the surrogate halves intentionally remains an
            // invalid byte boundary.
            output.byte_boundary[utf16_offset] = -1;
            ++utf16_offset;
        }
        byte_offset += width;
        output.byte_boundary[utf16_offset] = static_cast<std::int32_t>(byte_offset);
    }

    output.length = static_cast<std::int32_t>(utf16_offset);
    return true;
}

[[nodiscard]] bool size_face_logical(FT_Face face, const TypographyMetrics& metrics) noexcept {
    if (face == nullptr || metrics.size_q6 == 0U ||
        metrics.size_q6 > static_cast<std::uint32_t>(std::numeric_limits<FT_F26Dot6>::max())) {
        return false;
    }
    return FT_Set_Char_Size(
        face,
        0,
        static_cast<FT_F26Dot6>(metrics.size_q6),
        72U,
        72U) == 0;
}

[[nodiscard]] bool size_face_raster(
    FT_Face face,
    const TypographyMetrics& metrics,
    RasterScale scale) noexcept {
    if (face == nullptr || metrics.size_q6 == 0U || scale.numerator == 0U ||
        scale.denominator == 0U) {
        return false;
    }
    const std::uint64_t scaled =
        static_cast<std::uint64_t>(metrics.size_q6) * scale.numerator * 64U;
    const std::uint64_t rounded = (scaled + scale.denominator / 2U) / scale.denominator;
    if (rounded == 0U ||
        rounded > static_cast<std::uint64_t>(std::numeric_limits<FT_F26Dot6>::max())) {
        return false;
    }
    return FT_Set_Char_Size(
        face,
        0,
        static_cast<FT_F26Dot6>(rounded),
        72U,
        72U) == 0;
}

[[nodiscard]] std::uint16_t static_face_weight(FT_Face face) noexcept {
    const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face, ft_sfnt_os2));
    if (os2 == nullptr || os2->version == 0xFFFFU) return 400U;
    const std::uint32_t weight = std::clamp<std::uint32_t>(os2->usWeightClass, 1U, 1000U);
    return static_cast<std::uint16_t>(weight);
}

[[nodiscard]] constexpr bool line_separator(UChar value) noexcept {
    return value == static_cast<UChar>('\n') || value == static_cast<UChar>('\r') ||
        value == static_cast<UChar>(0x2028U) || value == static_cast<UChar>(0x2029U);
}

[[nodiscard]] std::int32_t trim_line_terminator(
    const Utf16Text& text,
    std::int32_t start,
    std::int32_t limit) noexcept {
    while (limit > start && line_separator(text.units[static_cast<std::size_t>(limit - 1)])) {
        --limit;
    }
    return limit;
}

[[nodiscard]] ShapedText blank_line(const ResolvedTextStyle& style) noexcept {
    ShapedText line {};
    line.line_height_q6 = style.metrics.line_height_q6;
    line.line_count = 1U;
    line.lines[0] = ShapedLine{.first_glyph = 0U, .glyph_count = 0U};
    return line;
}

} // namespace

struct LinuxTextBackend::Impl final {
    struct FaceSlot final {
        FontFamilyRole family {FontFamilyRole::interface};
        FontFaceId id {};
        FT_Face face {nullptr};
        hb_font_t* hb_font {nullptr};
        std::uint16_t weight {400U};
    };

    FT_Library library {nullptr};
    std::array<FaceSlot, family_slot_count> slots {};
    hb_buffer_t* shaping_buffer {nullptr};
    UBiDi* paragraph_bidi {nullptr};
    UBiDi* line_bidi {nullptr};
    UBreakIterator* line_breaker {nullptr};
    UBreakIterator* grapheme_breaker {nullptr};
    bool ready {false};

    ~Impl() {
        if (grapheme_breaker != nullptr) ubrk_close(grapheme_breaker);
        grapheme_breaker = nullptr;
        if (line_breaker != nullptr) ubrk_close(line_breaker);
        line_breaker = nullptr;
        if (line_bidi != nullptr) ubidi_close(line_bidi);
        line_bidi = nullptr;
        if (paragraph_bidi != nullptr) ubidi_close(paragraph_bidi);
        paragraph_bidi = nullptr;
        if (shaping_buffer != nullptr) hb_buffer_destroy(shaping_buffer);
        shaping_buffer = nullptr;

        for (auto& slot : slots) {
            if (slot.hb_font != nullptr) hb_font_destroy(slot.hb_font);
            slot.hb_font = nullptr;
            if (slot.face != nullptr) FT_Done_Face(slot.face);
            slot.face = nullptr;
        }
        if (library != nullptr) FT_Done_FreeType(library);
        library = nullptr;
    }

    [[nodiscard]] FaceSlot* slot_for(FontFamilyRole family) noexcept {
        const std::size_t index = family_index(family);
        return index < slots.size() ? &slots[index] : nullptr;
    }

    [[nodiscard]] const FaceSlot* slot_for(FontFamilyRole family) const noexcept {
        const std::size_t index = family_index(family);
        return index < slots.size() ? &slots[index] : nullptr;
    }

    [[nodiscard]] FaceSlot* slot_for(const FontFaceDescriptor& descriptor) noexcept {
        FaceSlot* slot = slot_for(descriptor.family);
        if (slot == nullptr || slot->id != descriptor.id) return nullptr;
        return slot;
    }

    [[nodiscard]] FontFamilyRole family_for_scalar(
        std::uint32_t scalar,
        const ResolvedTextStyle& style) const noexcept {
        for (std::size_t index = 0U; index < style.fallback.count; ++index) {
            const FontFamilyRole family = style.fallback.families[index];
            const FaceSlot* slot = slot_for(family);
            if (slot != nullptr && slot->face != nullptr &&
                FT_Get_Char_Index(slot->face, static_cast<FT_ULong>(scalar)) != 0U) {
                return family;
            }
        }
        return style.fallback.families[0];
    }

    [[nodiscard]] bool family_segments(
        const SemanticText& source,
        const ResolvedTextStyle& style,
        std::uint16_t byte_start,
        std::uint16_t byte_end,
        FamilySegments& output) const noexcept {
        output = {};
        std::size_t offset = byte_start;
        const std::size_t limit = byte_end;
        while (offset < limit) {
            std::uint32_t scalar = 0U;
            std::size_t scalar_width = 0U;
            if (!decode_utf8_scalar(source, offset, scalar, scalar_width) ||
                offset + scalar_width > limit) {
                return false;
            }
            const FontFamilyRole family = family_for_scalar(scalar, style);
            const std::size_t segment_start = offset;
            offset += scalar_width;
            while (offset < limit) {
                std::uint32_t next_scalar = 0U;
                std::size_t next_width = 0U;
                if (!decode_utf8_scalar(source, offset, next_scalar, next_width) ||
                    offset + next_width > limit) {
                    return false;
                }
                if (family_for_scalar(next_scalar, style) != family) break;
                offset += next_width;
            }
            if (output.count >= output.segments.size()) return false;
            output.segments[output.count++] = FamilySegment{
                .byte_start = static_cast<std::uint16_t>(segment_start),
                .byte_end = static_cast<std::uint16_t>(offset),
                .family = family,
            };
        }
        return output.count != 0U;
    }

    [[nodiscard]] bool shape_family_segment(
        const SemanticText& source,
        const ResolvedTextStyle& style,
        const FontFaceSet& faces,
        const FamilySegment& segment,
        TextDirection direction,
        ShapedText& output,
        std::uint64_t& width_q6) noexcept {
        if (faces.find(segment.family) == nullptr || segment.byte_start >= segment.byte_end ||
            output.run_count >= output.runs.size()) {
            return false;
        }
        FaceSlot* slot = slot_for(segment.family);
        if (slot == nullptr || slot->face == nullptr || slot->hb_font == nullptr ||
            shaping_buffer == nullptr || !size_face_logical(slot->face, style.metrics)) {
            return false;
        }
        hb_ft_font_changed(slot->hb_font);

        // LinuxTextBackend is renderer-owned serialized state. Reusing one
        // pre-created HarfBuzz buffer removes per-run buffer allocation/churn
        // from the hot shaping path without adding a worker pool or global
        // cache. A future parallel renderer must provide bounded per-worker
        // backend state instead of concurrently entering this object.
        hb_buffer_reset(shaping_buffer);
        hb_buffer_add_utf8(
            shaping_buffer,
            source.bytes.data(),
            static_cast<int>(source.length),
            static_cast<unsigned int>(segment.byte_start),
            static_cast<int>(segment.byte_end - segment.byte_start));
        hb_buffer_set_direction(shaping_buffer, hb_direction(direction));
        hb_buffer_set_cluster_level(shaping_buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
        hb_buffer_guess_segment_properties(shaping_buffer);
        hb_shape(slot->hb_font, shaping_buffer, nullptr, 0U);

        unsigned int glyph_count = 0U;
        const hb_glyph_info_t* infos =
            hb_buffer_get_glyph_infos(shaping_buffer, &glyph_count);
        const hb_glyph_position_t* positions =
            hb_buffer_get_glyph_positions(shaping_buffer, &glyph_count);
        if (infos == nullptr || positions == nullptr || glyph_count == 0U ||
            output.glyph_count + glyph_count > output.glyphs.size()) {
            return false;
        }

        const std::size_t first_glyph = output.glyph_count;
        for (unsigned int index = 0U; index < glyph_count; ++index) {
            const std::int64_t signed_advance = positions[index].x_advance;
            const std::uint64_t advance = signed_advance < 0
                ? static_cast<std::uint64_t>(-signed_advance)
                : static_cast<std::uint64_t>(signed_advance);
            if (advance > max_logical_dimension_q6 ||
                width_q6 > max_logical_dimension_q6 - advance ||
                infos[index].cluster < segment.byte_start || infos[index].cluster >= segment.byte_end) {
                return false;
            }
            output.glyphs[output.glyph_count] = ShapedGlyph{
                .glyph_id = infos[index].codepoint,
                .cluster_byte_offset = static_cast<std::uint16_t>(infos[index].cluster),
                .advance_q6 = static_cast<std::uint32_t>(advance),
                .offset_x_q6 = positions[index].x_offset,
                .offset_y_q6 = positions[index].y_offset,
                .family = segment.family,
            };
            ++output.glyph_count;
            width_q6 += advance;
        }

        output.runs[output.run_count] = ShapedRun{
            .first_glyph = static_cast<std::uint16_t>(first_glyph),
            .glyph_count = static_cast<std::uint16_t>(glyph_count),
            .text_byte_start = segment.byte_start,
            .text_byte_length = static_cast<std::uint16_t>(segment.byte_end - segment.byte_start),
            .family = segment.family,
            .direction = direction,
        };
        ++output.run_count;
        return true;
    }

    [[nodiscard]] bool shape_visual_line(
        const SemanticText& source,
        const ResolvedTextStyle& style,
        const FontFaceSet& faces,
        const Utf16Text& utf16,
        std::int32_t line_start,
        std::int32_t line_limit,
        ShapedText& output,
        std::uint64_t& width_q6) noexcept {
        output = {};
        output.line_height_q6 = style.metrics.line_height_q6;
        width_q6 = 0U;
        if (line_start < 0 || line_limit <= line_start || line_limit > utf16.length ||
            utf16.byte_boundary[static_cast<std::size_t>(line_start)] < 0 ||
            utf16.byte_boundary[static_cast<std::size_t>(line_limit)] < 0) {
            return false;
        }

        UErrorCode status = U_ZERO_ERROR;
        ubidi_setLine(paragraph_bidi, line_start, line_limit, line_bidi, &status);
        if (U_FAILURE(status)) return false;
        const std::int32_t run_count = ubidi_countRuns(line_bidi, &status);
        if (U_FAILURE(status) || run_count <= 0 ||
            static_cast<std::size_t>(run_count) > max_shaped_runs) {
            return false;
        }

        const std::int32_t line_length = line_limit - line_start;
        for (std::int32_t visual_index = 0; visual_index < run_count; ++visual_index) {
            std::int32_t logical_start = 0;
            std::int32_t logical_length = 0;
            const UBiDiDirection bidi_direction = ubidi_getVisualRun(
                line_bidi,
                visual_index,
                &logical_start,
                &logical_length);
            if (logical_start < 0 || logical_length <= 0 ||
                logical_start + logical_length > line_length) {
                return false;
            }
            const std::int32_t absolute_start = line_start + logical_start;
            const std::int32_t absolute_end = absolute_start + logical_length;
            const std::int32_t byte_start_i32 =
                utf16.byte_boundary[static_cast<std::size_t>(absolute_start)];
            const std::int32_t byte_end_i32 =
                utf16.byte_boundary[static_cast<std::size_t>(absolute_end)];
            if (byte_start_i32 < 0 || byte_end_i32 <= byte_start_i32 ||
                byte_end_i32 > source.length) {
                return false;
            }

            FamilySegments segments {};
            if (!family_segments(
                    source,
                    style,
                    static_cast<std::uint16_t>(byte_start_i32),
                    static_cast<std::uint16_t>(byte_end_i32),
                    segments)) {
                return false;
            }
            const TextDirection direction = text_direction(bidi_direction);
            if (direction == TextDirection::left_to_right) {
                for (std::size_t index = 0U; index < segments.count; ++index) {
                    if (!shape_family_segment(
                            source, style, faces, segments.segments[index], direction, output, width_q6)) {
                        return false;
                    }
                }
            } else {
                for (std::size_t index = segments.count; index > 0U; --index) {
                    if (!shape_family_segment(
                            source, style, faces, segments.segments[index - 1U], direction, output, width_q6)) {
                        return false;
                    }
                }
            }
        }

        if (output.glyph_count == 0U) return false;
        output.lines[0] = ShapedLine{
            .first_glyph = 0U,
            .glyph_count = static_cast<std::uint16_t>(output.glyph_count),
        };
        output.line_count = 1U;
        return true;
    }

    [[nodiscard]] bool collect_breaks(
        const Utf16Text& utf16,
        ParagraphWrapMode wrap,
        BreakPointSet& output) noexcept {
        output = {};
        if (wrap == ParagraphWrapMode::no_wrap) {
            output.points[0] = BreakPoint{.utf16_offset = utf16.length, .hard = true};
            output.count = 1U;
            return true;
        }

        UBreakIterator* iterator = wrap == ParagraphWrapMode::grapheme
            ? grapheme_breaker
            : line_breaker;
        if (iterator == nullptr) return false;
        UErrorCode status = U_ZERO_ERROR;
        ubrk_setText(iterator, utf16.units.data(), utf16.length, &status);
        if (U_FAILURE(status)) return false;
        (void)ubrk_first(iterator);
        for (std::int32_t boundary = ubrk_next(iterator);
             boundary != UBRK_DONE;
             boundary = ubrk_next(iterator)) {
            if (boundary <= 0 || boundary > utf16.length ||
                utf16.byte_boundary[static_cast<std::size_t>(boundary)] < 0 ||
                output.count >= output.points.size()) {
                return false;
            }
            const std::int32_t rule_status = ubrk_getRuleStatus(iterator);
            const bool hard = wrap == ParagraphWrapMode::word &&
                rule_status >= UBRK_LINE_HARD && rule_status < UBRK_LINE_HARD_LIMIT;
            output.points[output.count++] = BreakPoint{
                .utf16_offset = boundary,
                .hard = hard,
            };
        }
        return output.count != 0U &&
            output.points[output.count - 1U].utf16_offset == utf16.length;
    }

    [[nodiscard]] bool append_line(
        const ShapedText& line,
        ShapedText& paragraph) noexcept {
        if (line.line_count != 1U ||
            paragraph.line_count >= paragraph.lines.size() ||
            paragraph.glyph_count + line.glyph_count > paragraph.glyphs.size() ||
            paragraph.run_count + line.run_count > paragraph.runs.size() ||
            (line.glyph_count == 0U && line.run_count != 0U) ||
            (line.glyph_count != 0U && line.run_count == 0U)) {
            return false;
        }
        const std::size_t glyph_base = paragraph.glyph_count;
        for (std::size_t index = 0U; index < line.glyph_count; ++index) {
            paragraph.glyphs[paragraph.glyph_count++] = line.glyphs[index];
        }
        for (std::size_t index = 0U; index < line.run_count; ++index) {
            ShapedRun run = line.runs[index];
            const std::size_t adjusted = glyph_base + run.first_glyph;
            if (adjusted > std::numeric_limits<std::uint16_t>::max()) return false;
            run.first_glyph = static_cast<std::uint16_t>(adjusted);
            paragraph.runs[paragraph.run_count++] = run;
        }
        paragraph.lines[paragraph.line_count++] = ShapedLine{
            .first_glyph = static_cast<std::uint16_t>(glyph_base),
            .glyph_count = static_cast<std::uint16_t>(line.glyph_count),
        };
        return true;
    }

    [[nodiscard]] static bool resolve_font(
        void* context,
        FontFamilyRole family,
        FontFaceDescriptor& output) noexcept {
        auto* self = static_cast<Impl*>(context);
        if (self == nullptr || !self->ready) return false;
        const FaceSlot* slot = self->slot_for(family);
        if (slot == nullptr || slot->face == nullptr || slot->face->units_per_EM == 0U ||
            slot->face->units_per_EM > 16384U) {
            return false;
        }
        output = FontFaceDescriptor{
            .id = slot->id,
            .family = family,
            .units_per_em = static_cast<std::uint16_t>(slot->face->units_per_EM),
            .weight_min = slot->weight,
            .weight_max = slot->weight,
        };
        return true;
    }

    [[nodiscard]] static bool shape_paragraph(
        void* context,
        const SemanticText& source,
        const ResolvedTextStyle& style,
        const FontFaceSet& faces,
        const ParagraphConstraints& constraints,
        ShapedText& output) noexcept {
        auto* self = static_cast<Impl*>(context);
        if (self == nullptr || !self->ready || !semantic_text_valid(source) ||
            style.fallback.count == 0U || constraints.max_width_q6 == 0U ||
            constraints.max_lines == 0U) {
            return false;
        }

        if (source.empty()) {
            output = {};
            output.line_height_q6 = style.metrics.line_height_q6;
            return true;
        }

        Utf16Text utf16 {};
        if (!utf16_from_utf8(source, utf16) || utf16.length <= 0) return false;

        UBiDiLevel paragraph_level = UBIDI_DEFAULT_LTR;
        switch (constraints.base_direction) {
        case ParagraphBaseDirection::auto_detect:
            paragraph_level = UBIDI_DEFAULT_LTR;
            break;
        case ParagraphBaseDirection::left_to_right:
            paragraph_level = 0U;
            break;
        case ParagraphBaseDirection::right_to_left:
            paragraph_level = 1U;
            break;
        default:
            return false;
        }

        UErrorCode status = U_ZERO_ERROR;
        ubidi_setPara(
            self->paragraph_bidi,
            utf16.units.data(),
            utf16.length,
            paragraph_level,
            nullptr,
            &status);
        if (U_FAILURE(status)) return false;

        BreakPointSet breaks {};
        if (!self->collect_breaks(utf16, constraints.wrap, breaks)) return false;

        output = {};
        output.line_height_q6 = style.metrics.line_height_q6;
        std::int32_t line_start = 0;
        std::size_t break_index = 0U;
        bool truncated = false;

        while (line_start < utf16.length) {
            if (output.line_count >= constraints.max_lines) {
                truncated = true;
                break;
            }

            while (break_index < breaks.count &&
                   breaks.points[break_index].utf16_offset <= line_start) {
                ++break_index;
            }
            if (break_index >= breaks.count) return false;

            std::int32_t chosen_consumed = -1;
            ShapedText chosen_line {};
            bool chose_hard = false;

            if (constraints.wrap == ParagraphWrapMode::no_wrap) {
                const std::int32_t shape_limit = trim_line_terminator(
                    utf16, line_start, utf16.length);
                if (shape_limit <= line_start) {
                    chosen_line = blank_line(style);
                } else {
                    std::uint64_t width = 0U;
                    if (!self->shape_visual_line(
                            source, style, faces, utf16, line_start, shape_limit, chosen_line, width)) {
                        return false;
                    }
                }
                chosen_consumed = utf16.length;
            } else {
                for (std::size_t candidate_index = break_index;
                     candidate_index < breaks.count;
                     ++candidate_index) {
                    const BreakPoint candidate = breaks.points[candidate_index];
                    const std::int32_t shape_limit = trim_line_terminator(
                        utf16, line_start, candidate.utf16_offset);
                    if (shape_limit <= line_start) {
                        if (!candidate.hard) return false;
                        chosen_consumed = candidate.utf16_offset;
                        chosen_line = blank_line(style);
                        chose_hard = true;
                        break;
                    }

                    ShapedText candidate_line {};
                    std::uint64_t candidate_width = 0U;
                    if (!self->shape_visual_line(
                            source,
                            style,
                            faces,
                            utf16,
                            line_start,
                            shape_limit,
                            candidate_line,
                            candidate_width)) {
                        return false;
                    }
                    if (candidate_width > constraints.max_width_q6) break;
                    chosen_consumed = candidate.utf16_offset;
                    chosen_line = candidate_line;
                    chose_hard = candidate.hard;
                    if (candidate.hard || candidate.utf16_offset == utf16.length) break;
                }

                // A line-break opportunity can be wider than the viewport for
                // a long token. Fall back to Unicode grapheme boundaries only
                // as an emergency fit mechanism; the fallback is still bounded
                // and never splits a surrogate pair or UTF-8 scalar.
                if (chosen_consumed < 0 && constraints.wrap == ParagraphWrapMode::word) {
                    BreakPointSet graphemes {};
                    if (!self->collect_breaks(utf16, ParagraphWrapMode::grapheme, graphemes)) {
                        return false;
                    }
                    for (std::size_t candidate_index = 0U;
                         candidate_index < graphemes.count;
                         ++candidate_index) {
                        const std::int32_t candidate = graphemes.points[candidate_index].utf16_offset;
                        if (candidate <= line_start) continue;
                        const std::int32_t shape_limit = trim_line_terminator(
                            utf16, line_start, candidate);
                        if (shape_limit <= line_start) continue;
                        ShapedText candidate_line {};
                        std::uint64_t candidate_width = 0U;
                        if (!self->shape_visual_line(
                                source,
                                style,
                                faces,
                                utf16,
                                line_start,
                                shape_limit,
                                candidate_line,
                                candidate_width)) {
                            return false;
                        }
                        if (candidate_width > constraints.max_width_q6) break;
                        chosen_consumed = candidate;
                        chosen_line = candidate_line;
                        if (candidate == utf16.length) break;
                    }
                }

                if (chosen_consumed < 0) {
                    // Produce the smallest complete grapheme even when it does
                    // not fit. The core paragraph validator will turn this into
                    // paragraph_layout_limit rather than silently dropping text.
                    BreakPointSet graphemes {};
                    if (!self->collect_breaks(utf16, ParagraphWrapMode::grapheme, graphemes)) {
                        return false;
                    }
                    for (std::size_t candidate_index = 0U;
                         candidate_index < graphemes.count;
                         ++candidate_index) {
                        const std::int32_t candidate = graphemes.points[candidate_index].utf16_offset;
                        if (candidate <= line_start) continue;
                        const std::int32_t shape_limit = trim_line_terminator(
                            utf16, line_start, candidate);
                        if (shape_limit <= line_start) continue;
                        std::uint64_t ignored_width = 0U;
                        if (!self->shape_visual_line(
                                source,
                                style,
                                faces,
                                utf16,
                                line_start,
                                shape_limit,
                                chosen_line,
                                ignored_width)) {
                            return false;
                        }
                        chosen_consumed = candidate;
                        break;
                    }
                }
            }

            if (chosen_consumed <= line_start || !self->append_line(chosen_line, output)) {
                return false;
            }
            line_start = chosen_consumed;
            while (break_index < breaks.count &&
                   breaks.points[break_index].utf16_offset <= line_start) {
                ++break_index;
            }
            if (chose_hard) continue;
        }

        // Ellipsis needs an explicit synthetic-glyph/source-cluster contract so
        // assistive semantics and renderer clusters cannot diverge. Until that
        // contract exists, fail closed rather than drawing an untracked glyph.
        if (truncated && constraints.overflow == ParagraphOverflowMode::ellipsis) return false;
        return output.line_count != 0U;
    }

    [[nodiscard]] static bool shape_text(
        void* context,
        const SemanticText& source,
        const ResolvedTextStyle& style,
        const FontFaceSet& faces,
        ShapedText& output) noexcept {
        const ParagraphConstraints constraints{
            .max_width_q6 = max_logical_dimension_q6,
            .max_lines = 1U,
            .wrap = ParagraphWrapMode::no_wrap,
            .overflow = ParagraphOverflowMode::clip,
            .base_direction = ParagraphBaseDirection::auto_detect,
        };
        return shape_paragraph(context, source, style, faces, constraints, output);
    }

    [[nodiscard]] static bool resolve_line_metrics(
        void* context,
        const FontFaceDescriptor& descriptor,
        const TypographyMetrics& typography,
        FontLineMetrics& output) noexcept {
        auto* self = static_cast<Impl*>(context);
        if (self == nullptr || !self->ready) return false;
        FaceSlot* slot = self->slot_for(descriptor);
        if (slot == nullptr || !size_face_logical(slot->face, typography) ||
            slot->face->size == nullptr) {
            return false;
        }

        const FT_Pos ascender = slot->face->size->metrics.ascender;
        const FT_Pos descender = slot->face->size->metrics.descender;
        if (ascender <= 0) return false;
        const std::uint64_t ascent = static_cast<std::uint64_t>(ascender);
        const std::uint64_t descent = descender < 0
            ? static_cast<std::uint64_t>(-descender)
            : static_cast<std::uint64_t>(descender);
        if (ascent > max_logical_dimension_q6 || descent > max_logical_dimension_q6 ||
            ascent + descent > typography.line_height_q6) {
            return false;
        }
        output = FontLineMetrics{
            .ascent_q6 = static_cast<std::uint32_t>(ascent),
            .descent_q6 = static_cast<std::uint32_t>(descent),
            .line_gap_q6 = typography.line_height_q6 -
                static_cast<std::uint32_t>(ascent + descent),
        };
        return true;
    }

    [[nodiscard]] static bool resolve_glyph_mask(
        void* context,
        const FontFaceDescriptor& descriptor,
        const TypographyMetrics& typography,
        std::uint32_t glyph_id,
        RasterScale scale,
        GlyphMaskView& output) noexcept {
        auto* self = static_cast<Impl*>(context);
        if (self == nullptr || !self->ready ||
            glyph_id > static_cast<std::uint32_t>(std::numeric_limits<FT_UInt>::max())) {
            return false;
        }
        FaceSlot* slot = self->slot_for(descriptor);
        if (slot == nullptr || !size_face_raster(slot->face, typography, scale)) return false;

        if (FT_Load_Glyph(
                slot->face,
                static_cast<FT_UInt>(glyph_id),
                FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_LIGHT) != 0 ||
            FT_Render_Glyph(slot->face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
            return false;
        }
        const FT_Bitmap& bitmap = slot->face->glyph->bitmap;
        if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY || bitmap.num_grays != 256U ||
            bitmap.pitch < 0 || bitmap.width > max_glyph_mask_dimension ||
            bitmap.rows > max_glyph_mask_dimension ||
            bitmap.pitch > static_cast<int>(std::numeric_limits<std::uint16_t>::max()) ||
            slot->face->glyph->bitmap_left < std::numeric_limits<std::int16_t>::min() ||
            slot->face->glyph->bitmap_left > std::numeric_limits<std::int16_t>::max() ||
            slot->face->glyph->bitmap_top < std::numeric_limits<std::int16_t>::min() ||
            slot->face->glyph->bitmap_top > std::numeric_limits<std::int16_t>::max()) {
            return false;
        }
        const std::size_t byte_count =
            static_cast<std::size_t>(bitmap.pitch) * bitmap.rows;
        output = GlyphMaskView{
            .coverage = bitmap.buffer,
            .byte_count = byte_count,
            .width = static_cast<std::uint16_t>(bitmap.width),
            .height = static_cast<std::uint16_t>(bitmap.rows),
            .stride = static_cast<std::uint16_t>(bitmap.pitch),
            .bearing_x_px = static_cast<std::int16_t>(slot->face->glyph->bitmap_left),
            .bearing_top_px = static_cast<std::int16_t>(slot->face->glyph->bitmap_top),
        };
        return true;
    }
};

LinuxTextBackend::LinuxTextBackend(const LinuxFontFiles& files) noexcept
    : impl_(new (std::nothrow) Impl{}) {
    if (impl_ == nullptr) return;
    if (FT_Init_FreeType(&impl_->library) != 0) return;

    const std::array<const char*, family_slot_count> paths{{
        files.interface_path,
        files.display_path,
        files.international_path,
        files.symbols_path,
        files.monospace_path,
    }};
    const std::array<FontFamilyRole, family_slot_count> families{{
        FontFamilyRole::interface,
        FontFamilyRole::display,
        FontFamilyRole::international,
        FontFamilyRole::symbols,
        FontFamilyRole::monospace,
    }};

    for (std::size_t index = 0U; index < impl_->slots.size(); ++index) {
        if (paths[index] == nullptr || paths[index][0] == '\0') return;
        auto& slot = impl_->slots[index];
        slot.family = families[index];
        slot.id = FontFaceId{static_cast<std::uint32_t>(index + 1U)};
        if (FT_New_Face(impl_->library, paths[index], 0, &slot.face) != 0 ||
            slot.face == nullptr || slot.face->units_per_EM == 0U ||
            FT_Select_Charmap(slot.face, FT_ENCODING_UNICODE) != 0 ||
            FT_Set_Char_Size(slot.face, 0, default_face_size_26_6, 72U, 72U) != 0) {
            return;
        }
        slot.weight = static_face_weight(slot.face);
        slot.hb_font = hb_ft_font_create_referenced(slot.face);
        if (slot.hb_font == nullptr) return;
    }

    impl_->shaping_buffer = hb_buffer_create();
    if (impl_->shaping_buffer == nullptr ||
        hb_buffer_allocation_successful(impl_->shaping_buffer) == 0) {
        return;
    }

    UErrorCode status = U_ZERO_ERROR;
    impl_->paragraph_bidi = ubidi_openSized(
        static_cast<std::int32_t>(max_utf16_units),
        static_cast<std::int32_t>(max_shaped_runs),
        &status);
    impl_->line_bidi = ubidi_openSized(
        static_cast<std::int32_t>(max_utf16_units),
        static_cast<std::int32_t>(max_shaped_runs),
        &status);
    impl_->line_breaker = ubrk_open(UBRK_LINE, nullptr, nullptr, 0, &status);
    impl_->grapheme_breaker = ubrk_open(UBRK_CHARACTER, nullptr, nullptr, 0, &status);
    if (U_FAILURE(status) || impl_->paragraph_bidi == nullptr || impl_->line_bidi == nullptr ||
        impl_->line_breaker == nullptr || impl_->grapheme_breaker == nullptr) {
        return;
    }

    impl_->ready = true;
}

LinuxTextBackend::~LinuxTextBackend() {
    delete impl_;
    impl_ = nullptr;
}

bool LinuxTextBackend::valid() const noexcept {
    return impl_ != nullptr && impl_->ready;
}

FontProviderBackend LinuxTextBackend::font_provider() noexcept {
    return valid()
        ? FontProviderBackend{.context = impl_, .resolve = Impl::resolve_font}
        : FontProviderBackend{};
}

FontAwareTextShaperBackend LinuxTextBackend::text_shaper() noexcept {
    return valid()
        ? FontAwareTextShaperBackend{.context = impl_, .shape = Impl::shape_text}
        : FontAwareTextShaperBackend{};
}

FontAwareParagraphShaperBackend LinuxTextBackend::paragraph_shaper() noexcept {
    return valid()
        ? FontAwareParagraphShaperBackend{.context = impl_, .shape = Impl::shape_paragraph}
        : FontAwareParagraphShaperBackend{};
}

FontLineMetricsBackend LinuxTextBackend::line_metrics() noexcept {
    return valid()
        ? FontLineMetricsBackend{.context = impl_, .resolve = Impl::resolve_line_metrics}
        : FontLineMetricsBackend{};
}

GlyphMaskProviderBackend LinuxTextBackend::glyph_masks() noexcept {
    return valid()
        ? GlyphMaskProviderBackend{.context = impl_, .resolve = Impl::resolve_glyph_mask}
        : GlyphMaskProviderBackend{};
}

TextCommandRasterBackend LinuxTextBackend::command_backend() noexcept {
    return TextCommandRasterBackend{
        .fonts = font_provider(),
        .paragraphs = paragraph_shaper(),
        .line_metrics = line_metrics(),
        .glyphs = glyph_masks(),
    };
}

} // namespace os::ui::platform
