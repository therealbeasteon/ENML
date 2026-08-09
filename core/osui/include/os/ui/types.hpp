#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include <os/core/result.hpp>
#include <os/core/strong_id.hpp>

namespace os::ui {

struct UiNodeIdTag;
using UiNodeId = os::core::StrongId<UiNodeIdTag, std::uint32_t>;
struct StyleTokenIdTag;
using StyleTokenId = os::core::StrongId<StyleTokenIdTag, std::uint16_t>;

inline constexpr std::size_t max_ui_nodes = 256U;
inline constexpr std::size_t max_ui_children_per_node = 32U;
inline constexpr std::uint8_t max_ui_depth = 16U;
inline constexpr std::size_t max_semantic_text_bytes = 160U;

// M3.2 logical geometry is fixed-point: 64 units == 1 density-independent
// logical pixel. Applications never need Linux display DPI or device nodes to
// describe layout. Conversion to physical pixels remains a platform concern.
inline constexpr std::uint32_t logical_units_per_dp = 64U;
inline constexpr std::uint32_t max_logical_dimension_dp = 32768U;
inline constexpr std::uint32_t max_logical_dimension_q6 =
    max_logical_dimension_dp * logical_units_per_dp;

[[nodiscard]] constexpr std::uint32_t logical_from_dp(std::uint32_t dp) noexcept {
    if (dp > max_logical_dimension_dp) return 0U;
    return dp * logical_units_per_dp;
}

struct LogicalRect final {
    std::int32_t x_q6 {0};
    std::int32_t y_q6 {0};
    std::uint32_t width_q6 {0U};
    std::uint32_t height_q6 {0U};

    [[nodiscard]] constexpr bool nonempty() const noexcept {
        return width_q6 != 0U && height_q6 != 0U;
    }

    [[nodiscard]] constexpr bool bounded() const noexcept {
        if (!nonempty() || width_q6 > max_logical_dimension_q6 ||
            height_q6 > max_logical_dimension_q6) return false;
        const auto minimum = -static_cast<std::int64_t>(max_logical_dimension_q6);
        const auto maximum = static_cast<std::int64_t>(max_logical_dimension_q6);
        const auto x = static_cast<std::int64_t>(x_q6);
        const auto y = static_cast<std::int64_t>(y_q6);
        const auto right = x + static_cast<std::int64_t>(width_q6);
        const auto bottom = y + static_cast<std::int64_t>(height_q6);
        return x >= minimum && y >= minimum && right <= maximum && bottom <= maximum;
    }

    [[nodiscard]] friend constexpr bool operator==(const LogicalRect&, const LogicalRect&) = default;
};

struct LogicalInsets final {
    std::uint32_t top_q6 {0U};
    std::uint32_t right_q6 {0U};
    std::uint32_t bottom_q6 {0U};
    std::uint32_t left_q6 {0U};

    [[nodiscard]] friend constexpr bool operator==(const LogicalInsets&, const LogicalInsets&) = default;
};

struct LogicalViewport final {
    std::uint32_t width_q6 {0U};
    std::uint32_t height_q6 {0U};
    LogicalInsets safe_insets {};

    [[nodiscard]] constexpr bool valid() const noexcept {
        if (width_q6 == 0U || height_q6 == 0U ||
            width_q6 > max_logical_dimension_q6 ||
            height_q6 > max_logical_dimension_q6) return false;
        const auto horizontal = static_cast<std::uint64_t>(safe_insets.left_q6) +
            static_cast<std::uint64_t>(safe_insets.right_q6);
        const auto vertical = static_cast<std::uint64_t>(safe_insets.top_q6) +
            static_cast<std::uint64_t>(safe_insets.bottom_q6);
        return horizontal < static_cast<std::uint64_t>(width_q6) &&
            vertical < static_cast<std::uint64_t>(height_q6);
    }
};

enum class UiRole : std::uint8_t {
    root = 1U,
    container = 2U,
    text = 3U,
    image = 4U,
    button = 5U,
    toggle = 6U,
    text_field = 7U,
    list = 8U,
    list_item = 9U,
};

enum class UiAction : std::uint16_t {
    activate = 1U << 0U,
    focus = 1U << 1U,
    toggle = 1U << 2U,
    set_text = 1U << 3U,
    select = 1U << 4U,
};

using UiActionMask = std::uint16_t;

[[nodiscard]] constexpr UiActionMask action_mask(UiAction action) noexcept {
    return static_cast<UiActionMask>(action);
}

[[nodiscard]] constexpr bool has_action(UiActionMask mask, UiAction action) noexcept {
    return (mask & action_mask(action)) != 0U;
}

struct UiNodeState final {
    bool visible {true};
    bool enabled {true};
    bool focused {false};
    bool selected {false};
    bool checked {false};
    bool pressed {false};

    [[nodiscard]] friend constexpr bool operator==(const UiNodeState&, const UiNodeState&) = default;
};

struct SemanticText final {
    std::array<char, max_semantic_text_bytes> bytes {};
    std::uint16_t length {0U};

    [[nodiscard]] std::string_view view() const noexcept {
        return std::string_view{bytes.data(), static_cast<std::size_t>(length)};
    }

    [[nodiscard]] bool empty() const noexcept { return length == 0U; }
    [[nodiscard]] friend bool operator==(const SemanticText&, const SemanticText&) = default;
};

[[nodiscard]] os::core::Result<SemanticText> make_semantic_text(std::string_view text) noexcept;

struct UiNodeSpec final {
    UiRole role {UiRole::container};
    LogicalRect bounds {};
    UiActionMask actions {0U};
    UiNodeState state {};
    StyleTokenId style {};
    SemanticText label {};
    bool accessibility_hidden {false};
};

struct UiNodeDescriptor final {
    UiNodeId id {};
    UiNodeId parent {};
    std::uint8_t depth {0U};
    UiNodeSpec spec {};
};

struct UiEvent final {
    UiNodeId target {};
    UiAction action {UiAction::activate};
};

struct AccessibilityNode final {
    UiNodeId id {};
    UiNodeId parent {};
    UiRole role {UiRole::container};
    LogicalRect bounds {};
    UiNodeState state {};
    UiActionMask actions {0U};
    SemanticText label {};
};

struct AccessibilitySnapshot final {
    std::array<AccessibilityNode, max_ui_nodes> nodes {};
    std::size_t count {0U};
};

enum class PaneLayoutMode : std::uint8_t {
    single = 1U,
    dual = 2U,
};

struct ResponsivePolicy final {
    std::uint32_t two_pane_min_width_q6 {logical_from_dp(600U)};
    std::uint32_t pane_gap_q6 {logical_from_dp(16U)};
    std::uint32_t primary_min_width_q6 {logical_from_dp(220U)};
    std::uint32_t primary_max_width_q6 {logical_from_dp(360U)};
    std::uint32_t secondary_min_width_q6 {logical_from_dp(280U)};
    std::uint16_t primary_share_numerator {2U};
    std::uint16_t primary_share_denominator {5U};
};

struct PaneLayout final {
    PaneLayoutMode mode {PaneLayoutMode::single};
    LogicalRect primary {};
    LogicalRect secondary {};
};

[[nodiscard]] os::core::Result<PaneLayout> layout_list_detail(
    const LogicalViewport& viewport,
    const ResponsivePolicy& policy = {}) noexcept;

} // namespace os::ui
