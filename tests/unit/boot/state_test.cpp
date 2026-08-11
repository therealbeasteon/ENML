#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <os/boot/state.hpp>

// Boot state is security evidence, so these checks avoid assert(): the
// properties must hold identically in every build configuration.

namespace {

[[nodiscard]] bool check(bool condition, const char* what) noexcept {
    if (!condition) {
        std::fprintf(stderr, "boot state: %s\n", what);
    }
    return condition;
}

std::array<std::byte, os::boot::sha256_digest_bytes> digest_of(std::uint8_t seed) noexcept {
    std::array<std::byte, os::boot::sha256_digest_bytes> digest{};
    for (std::size_t index = 0U; index < digest.size(); ++index) {
        digest[index] = static_cast<std::byte>(
            static_cast<std::uint8_t>(seed + static_cast<std::uint8_t>(index)));
    }
    return digest;
}

// A complete, closed, verified chain: every link the format defines.
[[nodiscard]] os::core::Result<std::size_t>
encode_full_chain(
    os::core::MutableByteSpan output,
    os::boot::LifecycleState lifecycle,
    os::boot::VerificationResult verification) noexcept {
    os::boot::BootStateBuilder builder;
    builder.set_lifecycle(lifecycle);
    builder.set_verification(verification);
    builder.set_security_version(7U);

    const os::boot::BootStageKind kinds[] {
        os::boot::BootStageKind::first_stage,
        os::boot::BootStageKind::bootloader,
        os::boot::BootStageKind::kernel,
        os::boot::BootStageKind::configuration,
        os::boot::BootStageKind::root_filesystem,
    };
    std::uint8_t seed = 1U;
    for (const auto kind : kinds) {
        const auto digest = digest_of(seed);
        auto added = builder.add_stage(
            kind, os::boot::DigestAlgorithm::sha_256,
            os::core::ByteSpan{digest.data(), digest.size()});
        if (!added) {
            return added.error();
        }
        seed = static_cast<std::uint8_t>(seed + 40U);
    }
    return builder.encode(output);
}

} // namespace

int main() {
    std::array<std::byte, os::boot::max_boot_state_bytes> buffer{};

    // A default state is an unverified state. This is the property everything
    // else rests on: forgetting to parse must not look like a verified boot.
    const os::boot::BootStateV1 defaulted{};
    if (!check(!defaulted.verified(), "default state reported verified")) return 1;
    if (!check(defaulted.stage_count() == 0U, "default state had stages")) return 1;

    // An all-zero buffer must not decode. Zero is not a valid discriminant.
    for (auto& byte : buffer) { byte = std::byte{0}; }
    if (!check(!os::boot::parse_boot_state_v1({buffer.data(), buffer.size()}),
               "all-zero buffer parsed")) return 1;

    // Round trip of a complete closed chain.
    auto encoded = encode_full_chain(
        buffer, os::boot::LifecycleState::closed, os::boot::VerificationResult::verified);
    if (!check(static_cast<bool>(encoded), "full chain failed to encode")) return 1;
    const auto size = encoded.value();

    auto parsed = os::boot::parse_boot_state_v1({buffer.data(), size});
    if (!check(static_cast<bool>(parsed), "full chain failed to parse")) return 1;
    if (!check(parsed.value().verified(), "round trip lost verification")) return 1;
    if (!check(parsed.value().security_version() == 7U, "round trip lost security version")) return 1;
    if (!check(parsed.value().stage_count() == 5U, "round trip lost stages")) return 1;
    if (!check(static_cast<bool>(parsed.value().stage_of_kind(os::boot::BootStageKind::kernel)),
               "kernel stage missing after round trip")) return 1;

    // Trailing bytes are rejected: a record must be exactly its declared size.
    if (!check(!os::boot::parse_boot_state_v1({buffer.data(), size + 1U}),
               "trailing byte accepted")) return 1;
    if (!check(!os::boot::parse_boot_state_v1({buffer.data(), size - 1U}),
               "truncated record accepted")) return 1;

    // Every structural byte of the header rejects when corrupted.
    //
    // security_version is deliberately excluded, and the exclusion is the point:
    // the record is *not* self-authenticating. Flipping a byte of the version
    // counter produces a well-formed record with a different value, and the
    // parser cannot tell. Integrity comes from the record being produced by
    // trusted early boot and never crossing an untrusted boundary - not from
    // the encoding. Asserting otherwise would encode a guarantee the format
    // does not provide, which is worse than not testing it.
    constexpr std::size_t security_version_offset = 12U;
    constexpr std::size_t security_version_end = security_version_offset + 8U;

    for (std::size_t index = 0U; index < os::boot::boot_state_header_bytes_v1; ++index) {
        if (index >= security_version_offset && index < security_version_end) {
            continue;
        }
        const auto original = buffer[index];
        buffer[index] = static_cast<std::byte>(
            static_cast<std::uint8_t>(std::to_integer<std::uint8_t>(original) ^ 0xFFU));
        const bool rejected = !os::boot::parse_boot_state_v1({buffer.data(), size});
        buffer[index] = original;
        if (!check(rejected, "structural header corruption was accepted")) return 1;
    }

    // The counter field does parse when altered, and says so explicitly so the
    // next reader does not mistake this for an oversight.
    {
        const auto original = buffer[security_version_offset];
        buffer[security_version_offset] = static_cast<std::byte>(
            static_cast<std::uint8_t>(std::to_integer<std::uint8_t>(original) ^ 0xFFU));
        auto altered = os::boot::parse_boot_state_v1({buffer.data(), size});
        buffer[security_version_offset] = original;
        if (!check(static_cast<bool>(altered),
                   "counter corruption should parse; the record is not self-authenticating")) {
            return 1;
        }
        if (!check(altered.value().security_version() != parsed.value().security_version(),
                   "counter corruption did not change the counter")) return 1;
    }

    // A closed, verified device with an incomplete chain must fail closed.
    {
        os::boot::BootStateBuilder builder;
        builder.set_lifecycle(os::boot::LifecycleState::closed);
        builder.set_verification(os::boot::VerificationResult::verified);
        const auto digest = digest_of(3U);
        auto added = builder.add_stage(
            os::boot::BootStageKind::kernel, os::boot::DigestAlgorithm::sha_256,
            os::core::ByteSpan{digest.data(), digest.size()});
        if (!check(static_cast<bool>(added), "could not add stage")) return 1;
        auto partial = builder.encode(buffer);
        if (!check(static_cast<bool>(partial), "partial chain failed to encode")) return 1;
        if (!check(!os::boot::parse_boot_state_v1({buffer.data(), partial.value()}),
                   "closed device accepted an incomplete chain")) return 1;
    }

    // Verified with no measurements at all is incoherent.
    {
        os::boot::BootStateBuilder builder;
        builder.set_lifecycle(os::boot::LifecycleState::open);
        builder.set_verification(os::boot::VerificationResult::verified);
        auto empty = builder.encode(buffer);
        if (!check(static_cast<bool>(empty), "empty state failed to encode")) return 1;
        if (!check(!os::boot::parse_boot_state_v1({buffer.data(), empty.value()}),
                   "verified state with no stages accepted")) return 1;
    }

    // A duplicate stage kind means two digests claim the same link.
    {
        os::boot::BootStateBuilder builder;
        const auto digest = digest_of(9U);
        auto first = builder.add_stage(
            os::boot::BootStageKind::kernel, os::boot::DigestAlgorithm::sha_256,
            os::core::ByteSpan{digest.data(), digest.size()});
        if (!check(static_cast<bool>(first), "first stage rejected")) return 1;
        auto second = builder.add_stage(
            os::boot::BootStageKind::kernel, os::boot::DigestAlgorithm::sha_256,
            os::core::ByteSpan{digest.data(), digest.size()});
        if (!check(!second, "builder accepted a duplicate stage kind")) return 1;
    }

    // A failed state may carry measurements but can never satisfy verified().
    {
        auto failed = encode_full_chain(
            buffer, os::boot::LifecycleState::closed, os::boot::VerificationResult::failed);
        if (!check(static_cast<bool>(failed), "failed chain did not encode")) return 1;
        auto state = os::boot::parse_boot_state_v1({buffer.data(), failed.value()});
        if (!check(static_cast<bool>(state), "failed chain did not parse")) return 1;
        if (!check(!state.value().verified(), "failed state reported verified")) return 1;
        if (!check(state.value().stage_count() == 5U, "failed state lost measurements")) return 1;
    }

    return 0;
}
