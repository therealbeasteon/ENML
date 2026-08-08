#include <cstddef>
#include <cstdint>

#include <os/core/span.hpp>
#include <os/package/analyzer.hpp>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto* bytes = reinterpret_cast<const std::byte*>(data);
    const os::core::ByteSpan input(bytes, size);
    auto result = os::package::PackageManifestAnalyzer::analyze(input);
    static_cast<void>(result.has_value());
    return 0;
}
