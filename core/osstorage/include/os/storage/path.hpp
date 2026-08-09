#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <os/core/result.hpp>

namespace os::storage {

inline constexpr std::size_t max_relative_path_bytes = 1024U;
inline constexpr std::size_t max_path_segment_bytes = 255U;

// ENML private-storage paths are UTF-8, relative, slash-separated byte paths.
// M2.0 intentionally rejects '.', '..', empty segments, backslashes, NULs and
// absolute paths so Linux pathname quirks never become application authority.
class RelativePath final {
public:
    RelativePath() noexcept = default;

    [[nodiscard]] static os::core::Result<RelativePath>
    parse(std::string_view text) noexcept;

    [[nodiscard]] std::string_view view() const noexcept {
        return std::string_view(storage_.data(), size_);
    }

    [[nodiscard]] bool valid() const noexcept { return size_ != 0U; }

    [[nodiscard]] friend bool operator==(const RelativePath& lhs, const RelativePath& rhs) noexcept {
        return lhs.view() == rhs.view();
    }

private:
    std::array<char, max_relative_path_bytes> storage_ {};
    std::uint16_t size_ {0U};
};

} // namespace os::storage
