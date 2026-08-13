#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <os/core/result.hpp>
#include <os/core/span.hpp>

namespace os::kernel {

namespace fdt_errors {
inline constexpr std::uint32_t invalid_header = 60U;
inline constexpr std::uint32_t invalid_bounds = 61U;
inline constexpr std::uint32_t unsupported_version = 62U;
inline constexpr std::uint32_t malformed_structure = 63U;
inline constexpr std::uint32_t invalid_string = 64U;
inline constexpr std::uint32_t malformed_reservation = 65U;
} // namespace fdt_errors

struct FdtCallbacks final {
    void* context {nullptr};
    // Return false to stop the walk successfully after the current callback.
    bool (*begin_node)(void*, std::string_view, std::size_t) noexcept {nullptr};
    bool (*property)(void*, std::string_view, os::core::ByteSpan, std::size_t) noexcept {nullptr};
    bool (*end_node)(void*, std::size_t) noexcept {nullptr};
};

struct FdtReservationCallbacks final {
    void* context {nullptr};
    // Every callback receives one nonzero 64-bit address/size reservation from
    // the DTB memory-reservation block. Returning false stops successfully.
    bool (*reservation)(void*, std::uint64_t, std::uint64_t) noexcept {nullptr};
};

class FdtView final {
public:
    static constexpr std::uint32_t magic = 0xD00DFEEDU;
    static constexpr std::uint32_t token_begin_node = 1U;
    static constexpr std::uint32_t token_end_node = 2U;
    static constexpr std::uint32_t token_property = 3U;
    static constexpr std::uint32_t token_nop = 4U;
    static constexpr std::uint32_t token_end = 9U;
    static constexpr std::size_t header_bytes = 40U;

    [[nodiscard]] static os::core::Result<FdtView>
    parse(os::core::ByteSpan blob) noexcept;

    [[nodiscard]] os::core::Result<void>
    walk(const FdtCallbacks& callbacks) const noexcept;

    [[nodiscard]] os::core::Result<void>
    walk_reservations(const FdtReservationCallbacks& callbacks) const noexcept;

    [[nodiscard]] std::size_t total_size() const noexcept { return total_size_; }
    [[nodiscard]] std::uint32_t version() const noexcept { return version_; }

private:
    FdtView(os::core::ByteSpan blob, std::size_t total_size,
        std::size_t reservation_offset,
        std::size_t structure_offset, std::size_t structure_size,
        std::size_t strings_offset, std::size_t strings_size,
        std::uint32_t version) noexcept
        : blob_(blob), total_size_(total_size), reservation_offset_(reservation_offset),
          structure_offset_(structure_offset), structure_size_(structure_size),
          strings_offset_(strings_offset), strings_size_(strings_size), version_(version) {}

    os::core::ByteSpan blob_ {};
    std::size_t total_size_ {0U};
    std::size_t reservation_offset_ {0U};
    std::size_t structure_offset_ {0U};
    std::size_t structure_size_ {0U};
    std::size_t strings_offset_ {0U};
    std::size_t strings_size_ {0U};
    std::uint32_t version_ {0U};
};

} // namespace os::kernel
