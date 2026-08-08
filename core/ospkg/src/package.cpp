#include <os/package/package.hpp>

#include <algorithm>

#include <os/core/error.hpp>

namespace os::package {
namespace {

[[nodiscard]] constexpr os::core::Error package_error(std::uint32_t code) noexcept {
    return os::core::make_error(os::core::ErrorDomain::package, code);
}

[[nodiscard]] constexpr bool ascii_lower(char value) noexcept {
    return value >= 'a' && value <= 'z';
}

[[nodiscard]] constexpr bool ascii_digit(char value) noexcept {
    return value >= '0' && value <= '9';
}

} // namespace

os::core::Result<PackageId> PackageId::parse(std::string_view text) noexcept {
    if (text.empty() || text.size() > max_package_id_bytes) {
        return package_error(errors::invalid_package_id);
    }

    bool segment_start = true;
    for (const char value : text) {
        if (segment_start) {
            if (!ascii_lower(value)) {
                return package_error(errors::invalid_package_id);
            }
            segment_start = false;
            continue;
        }

        if (value == '.') {
            segment_start = true;
            continue;
        }

        if (!ascii_lower(value) && !ascii_digit(value) && value != '-' && value != '_') {
            return package_error(errors::invalid_package_id);
        }
    }

    if (segment_start) {
        return package_error(errors::invalid_package_id);
    }

    PackageId result;
    std::copy(text.begin(), text.end(), result.storage_.begin());
    result.size_ = static_cast<std::uint16_t>(text.size());
    return result;
}

bool SignerLineageId::valid() const noexcept {
    return std::any_of(bytes.begin(), bytes.end(), [](std::byte value) {
        return value != std::byte{0};
    });
}

bool ContentDigest::valid() const noexcept {
    return std::any_of(bytes.begin(), bytes.end(), [](std::byte value) {
        return value != std::byte{0};
    });
}

} // namespace os::package
