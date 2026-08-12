#include <os/kernel/fdt.hpp>

#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>

namespace os::kernel {
namespace {

[[nodiscard]] constexpr os::core::Error fdt_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::kernel, code);
}

[[nodiscard]] bool read_be32(
    os::core::ByteSpan bytes,
    std::size_t offset,
    std::uint32_t& out) noexcept {
    if (offset > bytes.size() || bytes.size() - offset < 4U) return false;
    out =
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) << 24U) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 16U) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2U])) << 8U) |
        static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3U]));
    return true;
}

[[nodiscard]] constexpr std::size_t align4(std::size_t value) noexcept {
    return (value + 3U) & ~static_cast<std::size_t>(3U);
}

[[nodiscard]] bool bounded_range(
    std::size_t offset,
    std::size_t length,
    std::size_t total) noexcept {
    return offset <= total && length <= total - offset;
}

[[nodiscard]] bool bounded_c_string(
    os::core::ByteSpan bytes,
    std::size_t begin,
    std::size_t end,
    std::string_view& out) noexcept {
    if (begin >= end || end > bytes.size()) return false;
    std::size_t cursor = begin;
    while (cursor < end && bytes[cursor] != std::byte{0}) ++cursor;
    if (cursor == end) return false;
    out = std::string_view(
        reinterpret_cast<const char*>(bytes.data() + begin), cursor - begin);
    return true;
}

} // namespace

os::core::Result<FdtView>
FdtView::parse(os::core::ByteSpan blob) noexcept {
    if (blob.size() < header_bytes) return fdt_error(fdt_errors::invalid_header);

    std::uint32_t header_magic = 0U;
    std::uint32_t total_size = 0U;
    std::uint32_t structure_offset = 0U;
    std::uint32_t strings_offset = 0U;
    std::uint32_t version = 0U;
    std::uint32_t last_compatible = 0U;
    std::uint32_t strings_size = 0U;
    std::uint32_t structure_size = 0U;

    if (!read_be32(blob, 0U, header_magic) || header_magic != magic ||
        !read_be32(blob, 4U, total_size) ||
        !read_be32(blob, 8U, structure_offset) ||
        !read_be32(blob, 12U, strings_offset) ||
        !read_be32(blob, 20U, version) ||
        !read_be32(blob, 24U, last_compatible) ||
        !read_be32(blob, 32U, strings_size) ||
        !read_be32(blob, 36U, structure_size)) {
        return fdt_error(fdt_errors::invalid_header);
    }

    if (total_size < header_bytes || total_size > blob.size()) {
        return fdt_error(fdt_errors::invalid_bounds);
    }
    // Version 17 is the stable modern header layout containing explicit
    // structure/string sizes. Newer versions remain backwards compatible only
    // if they advertise compatibility with <=17.
    if (version < 17U || last_compatible > 17U) {
        return fdt_error(fdt_errors::unsupported_version);
    }
    if (!bounded_range(structure_offset, structure_size, total_size) ||
        !bounded_range(strings_offset, strings_size, total_size) ||
        (structure_offset & 3U) != 0U) {
        return fdt_error(fdt_errors::invalid_bounds);
    }

    return FdtView{
        blob.first(total_size), total_size,
        structure_offset, structure_size,
        strings_offset, strings_size, version};
}

os::core::Result<void>
FdtView::walk(const FdtCallbacks& callbacks) const noexcept {
    const std::size_t structure_end = structure_offset_ + structure_size_;
    const std::size_t strings_end = strings_offset_ + strings_size_;
    std::size_t cursor = structure_offset_;
    std::size_t depth = 0U;

    while (cursor < structure_end) {
        std::uint32_t token = 0U;
        if (!read_be32(blob_, cursor, token)) {
            return fdt_error(fdt_errors::malformed_structure);
        }
        cursor += 4U;

        if (token == token_begin_node) {
            std::string_view name{};
            if (!bounded_c_string(blob_, cursor, structure_end, name)) {
                return fdt_error(fdt_errors::malformed_structure);
            }
            const std::size_t consumed = name.size() + 1U;
            if (cursor > structure_end || align4(consumed) > structure_end - cursor) {
                return fdt_error(fdt_errors::malformed_structure);
            }
            if (callbacks.begin_node != nullptr &&
                !callbacks.begin_node(callbacks.context, name, depth)) return {};
            ++depth;
            cursor += align4(consumed);
            continue;
        }

        if (token == token_end_node) {
            if (depth == 0U) return fdt_error(fdt_errors::malformed_structure);
            --depth;
            if (callbacks.end_node != nullptr &&
                !callbacks.end_node(callbacks.context, depth)) return {};
            continue;
        }

        if (token == token_property) {
            if (depth == 0U || cursor > structure_end || structure_end - cursor < 8U) {
                return fdt_error(fdt_errors::malformed_structure);
            }
            std::uint32_t length = 0U;
            std::uint32_t name_offset = 0U;
            if (!read_be32(blob_, cursor, length) ||
                !read_be32(blob_, cursor + 4U, name_offset)) {
                return fdt_error(fdt_errors::malformed_structure);
            }
            cursor += 8U;
            if (name_offset >= strings_size_) {
                return fdt_error(fdt_errors::invalid_string);
            }
            std::string_view property_name{};
            if (!bounded_c_string(
                    blob_, strings_offset_ + name_offset, strings_end, property_name)) {
                return fdt_error(fdt_errors::invalid_string);
            }
            if (length > structure_end - cursor || align4(length) > structure_end - cursor) {
                return fdt_error(fdt_errors::malformed_structure);
            }
            const auto value = blob_.subspan(cursor, length);
            if (callbacks.property != nullptr &&
                !callbacks.property(callbacks.context, property_name, value, depth - 1U)) return {};
            cursor += align4(length);
            continue;
        }

        if (token == token_nop) continue;
        if (token == token_end) {
            if (depth != 0U) return fdt_error(fdt_errors::malformed_structure);
            return {};
        }
        return fdt_error(fdt_errors::malformed_structure);
    }

    return fdt_error(fdt_errors::malformed_structure);
}

} // namespace os::kernel
