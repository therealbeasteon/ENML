#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <os/core/identity.hpp>
#include <os/core/strong_id.hpp>

namespace os::display {

struct SurfaceIdTag;
using SurfaceId = os::core::StrongId<SurfaceIdTag, std::uint64_t>;
struct BufferIdTag;
using BufferId = os::core::StrongId<BufferIdTag, std::uint64_t>;

// M3 display object ids are service-generation scoped. The high 32 bits name
// the Supervisor generation and the low 32 bits are a generation-local serial.
// Generation zero and serial zero are reserved as invalid. This keeps the
// compact 64-bit wire shape while making stale ids from a dead compositor
// generation structurally unable to alias objects in its replacement.
inline constexpr std::uint64_t display_object_local_mask = 0xFFFF'FFFFULL;
inline constexpr std::uint64_t max_display_object_generation =
    static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());

[[nodiscard]] constexpr bool valid_display_generation(std::uint64_t generation) noexcept {
    return generation != 0U && generation <= max_display_object_generation;
}

[[nodiscard]] constexpr std::uint64_t make_display_object_value(
    std::uint64_t generation,
    std::uint32_t serial) noexcept {
    if (!valid_display_generation(generation) || serial == 0U) return 0U;
    return (generation << 32U) | static_cast<std::uint64_t>(serial);
}

[[nodiscard]] constexpr std::uint32_t display_object_generation(std::uint64_t value) noexcept {
    return static_cast<std::uint32_t>(value >> 32U);
}

[[nodiscard]] constexpr std::uint32_t display_object_serial(std::uint64_t value) noexcept {
    return static_cast<std::uint32_t>(value & display_object_local_mask);
}

[[nodiscard]] constexpr bool valid_display_object_value(std::uint64_t value) noexcept {
    return display_object_generation(value) != 0U && display_object_serial(value) != 0U;
}

inline constexpr std::uint32_t max_display_dimension_px = 16384U;
inline constexpr std::size_t max_surfaces = 64U;
inline constexpr std::size_t max_surfaces_per_principal = 8U;
inline constexpr std::size_t max_damage_rectangles = 8U;
inline constexpr std::uint8_t max_frame_buffer_slots = 3U;
inline constexpr std::size_t max_shared_buffers = 48U;
inline constexpr std::size_t max_shared_buffers_per_principal = 8U;
inline constexpr std::uint64_t max_shared_buffer_bytes = 24ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t max_shared_buffer_bytes_per_principal = 48ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t max_shared_buffer_bytes_global = 128ULL * 1024ULL * 1024ULL;

struct PixelSize final {
    std::uint32_t width {0U};
    std::uint32_t height {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return width != 0U && height != 0U &&
            width <= max_display_dimension_px && height <= max_display_dimension_px;
    }

    [[nodiscard]] friend constexpr bool operator==(const PixelSize&, const PixelSize&) = default;
};

struct Rect final {
    std::int32_t x {0};
    std::int32_t y {0};
    std::uint32_t width {0U};
    std::uint32_t height {0U};

    [[nodiscard]] constexpr bool nonempty() const noexcept {
        return width != 0U && height != 0U;
    }

    [[nodiscard]] friend constexpr bool operator==(const Rect&, const Rect&) = default;
};

struct SafeInsets final {
    std::uint32_t top {0U};
    std::uint32_t right {0U};
    std::uint32_t bottom {0U};
    std::uint32_t left {0U};

    [[nodiscard]] friend constexpr bool operator==(const SafeInsets&, const SafeInsets&) = default;
};

enum class SurfaceRole : std::uint8_t {
    application = 1U,
    popup = 2U,
    system_chrome = 3U,
    secure_system = 4U,
};

// This is compositor-derived trust metadata, not a drawable application style.
// Applications submit pixels but cannot select this classification. A later
// compositor backend can use it to add non-buffer trust attribution so drawing
// a lookalike inside an application surface is not sufficient to mint trusted
// system presentation.
enum class TrustedPresentation : std::uint8_t {
    none = 0U,
    system_chrome = 1U,
    secure_system = 2U,
};

enum class SurfaceVisibility : std::uint8_t {
    hidden = 0U,
    visible = 1U,
};

enum class PixelFormat : std::uint32_t {
    rgba8888 = 1U,
    rgbx8888 = 2U,
};

struct DisplayConfiguration final {
    PixelSize size {};
    SafeInsets safe_insets {};
    std::uint32_t refresh_millihz {0U};
    std::uint64_t compositor_margin_ns {0U};
};

struct TrustedUiPrincipals final {
    os::core::PrincipalId shell {};
    os::core::PrincipalId secure_ui {};
};

struct CreateSurfaceRequest final {
    SurfaceRole role {SurfaceRole::application};
    SurfaceId parent {};
    Rect bounds {};
    bool accepts_input {true};
};

struct SurfaceDescriptor final {
    SurfaceId id {};
    os::core::PeerIdentity owner {};
    SurfaceRole role {SurfaceRole::application};
    SurfaceId parent {};
    Rect bounds {};
    SurfaceVisibility visibility {SurfaceVisibility::hidden};
    bool accepts_input {true};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return valid_display_object_value(id.value()) &&
            os::core::valid_peer_identity(owner) && bounds.nonempty();
    }
};

struct BufferDescriptor final {
    BufferId id {};
    os::core::PeerIdentity owner {};
    PixelSize size {};
    PixelFormat format {PixelFormat::rgba8888};
    std::uint32_t stride_bytes {0U};
    std::uint64_t byte_size {0U};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return valid_display_object_value(id.value()) &&
            os::core::valid_peer_identity(owner) && size.valid() &&
            stride_bytes != 0U && byte_size != 0U && byte_size <= max_shared_buffer_bytes;
    }
};

struct FrameSubmission final {
    SurfaceId surface {};
    BufferId buffer {};
    std::uint64_t sequence {0U};
    std::uint8_t buffer_slot {0U};
    std::uint8_t damage_count {0U};
    std::array<Rect, max_damage_rectangles> damage {};
};

struct FrameDeadline final {
    std::uint64_t next_vsync_ns {0U};
    std::uint64_t submission_deadline_ns {0U};

    [[nodiscard]] friend constexpr bool operator==(const FrameDeadline&, const FrameDeadline&) = default;
};

struct FrameReceipt final {
    SurfaceId surface {};
    BufferId buffer {};
    std::uint64_t sequence {0U};
    FrameDeadline deadline {};
};

struct SceneEntry final {
    SurfaceDescriptor surface {};
    BufferId buffer {};
    std::uint64_t frame_sequence {0U};
    std::uint8_t buffer_slot {0U};
    bool has_frame {false};
    bool capture_allowed {true};
    TrustedPresentation trusted_presentation {TrustedPresentation::none};
};

struct SceneSnapshot final {
    std::array<SceneEntry, max_surfaces> entries {};
    std::size_t count {0U};
};

} // namespace os::display
