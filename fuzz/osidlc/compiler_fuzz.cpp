#include <cstddef>
#include <cstdint>
#include <string_view>

#include <osidlc/compiler.hpp>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto source = std::string_view{
        reinterpret_cast<const char*>(data), size};
    (void)osidlc::compile_text(source, "<fuzz>");
    return 0;
}
