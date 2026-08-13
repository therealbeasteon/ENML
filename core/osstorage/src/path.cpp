#include <os/storage/path.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

#include <os/core/span.hpp>
#include <os/ipc/decoder.hpp>
#include <os/storage/error.hpp>

namespace os::storage {

os::core::Result<RelativePath>
RelativePath::parse(std::string_view text) noexcept {
    if (text.empty() || text.front() == '/' || text.back() == '/') {
        return storage_error(errors::invalid_path);
    }
    if (text.size() > max_relative_path_bytes) {
        return storage_error(errors::path_too_long);
    }

    const auto bytes = os::core::ByteSpan(
        reinterpret_cast<const std::byte*>(text.data()), text.size());
    if (!os::ipc::is_valid_utf8(bytes)) {
        return storage_error(errors::invalid_path);
    }

    std::size_t segment_start = 0U;
    for (std::size_t index = 0U; index <= text.size(); ++index) {
        const bool end = index == text.size();
        const char value = end ? '/' : text[index];

        if (!end && (value == '\0' || value == '\\')) {
            return storage_error(errors::invalid_path);
        }
        if (value != '/') continue;

        const std::size_t segment_size = index - segment_start;
        if (segment_size == 0U) {
            return storage_error(errors::invalid_path);
        }
        if (segment_size > max_path_segment_bytes) {
            return storage_error(errors::segment_too_long);
        }

        const auto segment = text.substr(segment_start, segment_size);
        if (segment == "." || segment == "..") {
            return storage_error(errors::path_escape);
        }
        segment_start = index + 1U;
    }

    RelativePath result;
    std::copy(text.begin(), text.end(), result.storage_.begin());
    result.size_ = static_cast<std::uint16_t>(text.size());
    return result;
}

os::core::Result<RelativePath>
join_relative_paths(const RelativePath& parent, const RelativePath& child) noexcept {
    if (!parent.valid() || !child.valid()) {
        return storage_error(errors::invalid_path);
    }
    const auto parent_view = parent.view();
    const auto child_view = child.view();
    if (parent_view.size() + 1U + child_view.size() > max_relative_path_bytes) {
        return storage_error(errors::path_too_long);
    }

    std::array<char, max_relative_path_bytes> storage{};
    std::size_t cursor = 0U;
    for (char value : parent_view) storage[cursor++] = value;
    storage[cursor++] = '/';
    for (char value : child_view) storage[cursor++] = value;
    return RelativePath::parse(std::string_view(storage.data(), cursor));
}

} // namespace os::storage
