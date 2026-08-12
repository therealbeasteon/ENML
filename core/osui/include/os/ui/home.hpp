#pragma once

#include <cstdint>

namespace os::ui {

enum class HomeRegion : std::uint8_t {
    field = 0U,
    shelf = 1U,
    dock = 2U,
};

enum class HomeObjectKind : std::uint8_t {
    application = 0U,
    widget = 1U,
    folder = 2U,
    conversation = 3U,
    contact = 4U,
    document = 5U,
    setting = 6U,
    action = 7U,
    trusted_system_tile = 8U,
};

enum class HomePrivacyClass : std::uint8_t {
    public_metadata = 0U,
    private_metadata = 1U,
    sensitive_preview = 2U,
    secret = 3U,
};

struct HomeObjectId final {
    std::uint64_t value {0U};

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0U; }
    friend constexpr bool operator==(HomeObjectId, HomeObjectId) noexcept = default;
};

struct HomeObject final {
    HomeObjectId id {};
    HomeObjectKind kind {HomeObjectKind::application};
    HomeRegion preferred_region {HomeRegion::field};
    HomePrivacyClass privacy {HomePrivacyClass::public_metadata};
    std::uint16_t preferred_span_x {1U};
    std::uint16_t preferred_span_y {1U};
    bool user_pinned {false};
    bool remote_enrichment_allowed {false};
};

[[nodiscard]] constexpr bool home_object_valid(const HomeObject& object) noexcept {
    if (!object.id.valid() || object.preferred_span_x == 0U || object.preferred_span_y == 0U) {
        return false;
    }
    if (object.privacy == HomePrivacyClass::secret && object.remote_enrichment_allowed) {
        return false;
    }
    return true;
}

[[nodiscard]] constexpr bool preview_allowed(
    HomePrivacyClass privacy,
    bool device_locked) noexcept {
    if (device_locked) {
        return privacy == HomePrivacyClass::public_metadata;
    }
    return privacy != HomePrivacyClass::secret;
}

[[nodiscard]] constexpr bool remote_query_allowed(const HomeObject& object) noexcept {
    return object.remote_enrichment_allowed &&
           object.privacy == HomePrivacyClass::public_metadata;
}

} // namespace os::ui
