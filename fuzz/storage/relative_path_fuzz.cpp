#include <cstddef>
#include <cstdint>
#include <string_view>

#include <os/storage/path.hpp>

// RelativePath::parse is the confinement boundary for private storage: it is
// what stops a caller-supplied byte string from escaping its storage root. It
// rejects absolute paths, empty segments, "." and "..", NUL, backslash,
// malformed and overlong UTF-8, and oversized segments.
//
// The parser is a pure function over untrusted text with no I/O, which makes it
// both the highest-value and the cheapest target in the tree to fuzz.
//
// Round-tripping the accepted result is deliberate. A crash is not the only
// interesting failure here: if parse() accepts input and then view() reports
// bytes that no longer satisfy the same rules, confinement has been broken
// without any memory-safety error occurring. Re-parsing the accepted output
// asserts idempotence, so an accept/serialize disagreement shows up as a
// divergence rather than passing silently.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto text = std::string_view{
        reinterpret_cast<const char*>(data), size};

    auto parsed = os::storage::RelativePath::parse(text);
    if (!parsed) {
        return 0;
    }

    const auto accepted = parsed.value().view();

    auto reparsed = os::storage::RelativePath::parse(accepted);
    if (!reparsed) {
        // An accepted path that no longer parses means parse() and view()
        // disagree about what is confined.
        __builtin_trap();
    }
    if (reparsed.value().view() != accepted) {
        // Parsing is not idempotent: the second pass produced different bytes.
        __builtin_trap();
    }

    return 0;
}
