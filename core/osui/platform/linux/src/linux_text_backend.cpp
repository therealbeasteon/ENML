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

namespace os::ui::platform {
namespace {

inline constexpr std::size_t family_slot_count = 5U;
inline constexpr FT_F26Dot6 default_face_size_26_6 = 16L * 64L;

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

[[nodiscard]] constexpr TextDirection text_direction(hb_direction_t direction) noexcept {
    return HB_DIRECTION_IS_BACKWARD(direction)
        ? TextDirection::right_to_left
        : TextDirection::left_to_right;
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
    bool ready {false};

    ~Impl() {
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

    [[nodiscard]] static bool shape_text(
        void* context,
        const SemanticText& source,
        const ResolvedTextStyle& style,
        const FontFaceSet& faces,
        ShapedText& output) noexcept {
        auto* self = static_cast<Impl*>(context);
        if (self == nullptr || !self->ready || !semantic_text_valid(source) ||
            style.fallback.count == 0U || source.empty()) {
            if (self != nullptr && self->ready && source.empty()) {
                output = ShapedText{};
                output.line_height_q6 = style.metrics.line_height_q6;
                return true;
            }
            return false;
        }

        output = ShapedText{};
        output.line_height_q6 = style.metrics.line_height_q6;

        std::size_t segment_start = 0U;
        while (segment_start < static_cast<std::size_t>(source.length)) {
            std::uint32_t scalar = 0U;
            std::size_t scalar_width = 0U;
            if (!decode_utf8_scalar(source, segment_start, scalar, scalar_width)) return false;
            const FontFamilyRole family = self->family_for_scalar(scalar, style);
            if (faces.find(family) == nullptr) return false;

            std::size_t segment_end = segment_start + scalar_width;
            while (segment_end < static_cast<std::size_t>(source.length)) {
                std::uint32_t next_scalar = 0U;
                std::size_t next_width = 0U;
                if (!decode_utf8_scalar(source, segment_end, next_scalar, next_width)) return false;
                if (self->family_for_scalar(next_scalar, style) != family) break;
                segment_end += next_width;
            }

            FaceSlot* slot = self->slot_for(family);
            if (slot == nullptr || slot->face == nullptr || slot->hb_font == nullptr ||
                !size_face_logical(slot->face, style.metrics)) {
                return false;
            }
            hb_ft_font_changed(slot->hb_font);

            hb_buffer_t* buffer = hb_buffer_create();
            if (buffer == nullptr || hb_buffer_allocation_successful(buffer) == 0) {
                if (buffer != nullptr) hb_buffer_destroy(buffer);
                return false;
            }
            hb_buffer_add_utf8(
                buffer,
                source.bytes.data(),
                static_cast<int>(source.length),
                static_cast<unsigned int>(segment_start),
                static_cast<int>(segment_end - segment_start));
            hb_buffer_guess_segment_properties(buffer);
            hb_shape(slot->hb_font, buffer, nullptr, 0U);

            unsigned int glyph_count = 0U;
            const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &glyph_count);
            const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &glyph_count);
            if (infos == nullptr || positions == nullptr || glyph_count == 0U ||
                output.glyph_count + glyph_count > output.glyphs.size() ||
                output.run_count >= output.runs.size()) {
                hb_buffer_destroy(buffer);
                return false;
            }

            const std::size_t first_glyph = output.glyph_count;
            for (unsigned int index = 0U; index < glyph_count; ++index) {
                const hb_position_t advance = positions[index].x_advance;
                if (advance < 0 ||
                    static_cast<std::uint64_t>(advance) > max_logical_dimension_q6 ||
                    infos[index].cluster >= source.length) {
                    hb_buffer_destroy(buffer);
                    return false;
                }
                output.glyphs[output.glyph_count] = ShapedGlyph{
                    .glyph_id = infos[index].codepoint,
                    .cluster_byte_offset = static_cast<std::uint16_t>(infos[index].cluster),
                    .advance_q6 = static_cast<std::uint32_t>(advance),
                    .offset_x_q6 = positions[index].x_offset,
                    .offset_y_q6 = positions[index].y_offset,
                    .family = family,
                };
                ++output.glyph_count;
            }

            output.runs[output.run_count] = ShapedRun{
                .first_glyph = static_cast<std::uint16_t>(first_glyph),
                .glyph_count = static_cast<std::uint16_t>(glyph_count),
                .text_byte_start = static_cast<std::uint16_t>(segment_start),
                .text_byte_length = static_cast<std::uint16_t>(segment_end - segment_start),
                .family = family,
                .direction = text_direction(hb_buffer_get_direction(buffer)),
            };
            ++output.run_count;
            hb_buffer_destroy(buffer);
            segment_start = segment_end;
        }

        if (output.glyph_count == 0U) return false;
        output.lines[0] = ShapedLine{
            .first_glyph = 0U,
            .glyph_count = static_cast<std::uint16_t>(output.glyph_count),
        };
        output.line_count = 1U;
        return true;
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

} // namespace os::ui::platform
