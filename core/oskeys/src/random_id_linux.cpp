#include <os/keys/random_id_source.hpp>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>

#include <sys/random.h>

#include <os/keys/error.hpp>

namespace os::keys {
namespace {

[[nodiscard]] constexpr std::uint64_t decode_u64_le(
    const std::array<std::byte, 16U>& bytes,
    std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(
            static_cast<std::uint8_t>(bytes[offset + index])) << (index * 8U);
    }
    return value;
}

} // namespace

os::core::Result<KeyId> RandomKeyIdSource::next() noexcept {
    // Retry the vanishingly unlikely all-zero output without introducing a
    // zero/sentinel KeyId into the public namespace.
    for (std::size_t attempt = 0U; attempt < 8U; ++attempt) {
        std::array<std::byte, 16U> bytes{};
        std::size_t written = 0U;
        while (written < bytes.size()) {
            ssize_t result = -1;
            do {
                result = ::getrandom(
                    bytes.data() + static_cast<std::ptrdiff_t>(written),
                    bytes.size() - written,
                    0U);
            } while (result < 0 && errno == EINTR);
            if (result <= 0) return key_error(errors::id_generation_failed);
            written += static_cast<std::size_t>(result);
        }

        const KeyId id{
            decode_u64_le(bytes, 0U),
            decode_u64_le(bytes, 8U),
        };
        if (id.valid()) return id;
    }
    return key_error(errors::id_generation_failed);
}

} // namespace os::keys
