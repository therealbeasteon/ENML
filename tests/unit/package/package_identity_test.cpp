#include <array>
#include <cassert>
#include <cstddef>
#include <string>

#include <os/core/error.hpp>
#include <os/package/package.hpp>

namespace pkg = os::package;

int main() {
    auto valid = pkg::PackageId::parse("com.emnl.notes");
    assert(valid);
    assert(valid.value().view() == "com.emnl.notes");

    assert(pkg::PackageId::parse("a"));
    assert(pkg::PackageId::parse("com.example.app_2"));
    assert(pkg::PackageId::parse("com.example.app-beta"));

    const std::array<const char*, 8> invalid{
        "", ".com.emnl", "com..emnl", "com.emnl.", "Com.emnl",
        "1com.emnl", "com.$bad", "com.2bad"
    };
    for (const char* text : invalid) {
        auto result = pkg::PackageId::parse(text);
        assert(!result);
        assert(result.error().domain == os::core::ErrorDomain::package);
        assert(result.error().code == pkg::errors::invalid_package_id);
    }

    std::string oversized(pkg::max_package_id_bytes + 1U, 'a');
    assert(!pkg::PackageId::parse(oversized));

    pkg::SignerLineageId signer{};
    assert(!signer.valid());
    signer.bytes[0] = std::byte{1};
    assert(signer.valid());

    pkg::ContentDigest digest{};
    assert(!digest.valid());
    digest.bytes[31] = std::byte{0xA5};
    assert(digest.valid());
    return 0;
}
