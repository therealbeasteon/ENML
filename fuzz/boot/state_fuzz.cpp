#include <cstddef>
#include <cstdint>

#include <os/boot/state.hpp>
#include <os/core/span.hpp>

// The boot state parser consumes the one record every ENML security property
// ultimately rests on. It must fail closed on anything malformed, and the
// properties below must hold for every accepted input, not merely for the
// inputs a test author thought of.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto bytes = os::core::ByteSpan{
        reinterpret_cast<const std::byte*>(data), size};

    auto parsed = os::boot::parse_boot_state_v1(bytes);
    if (!parsed) {
        return 0;
    }
    const auto state = parsed.value();

    // A verified state must carry at least one measurement. Accepting one
    // without would let an empty record assert a successful boot.
    if (state.verified() && state.stage_count() == 0U) {
        __builtin_trap();
    }

    // A closed, verified device must measure every link.
    if (state.verified() && state.lifecycle() == os::boot::LifecycleState::closed) {
        const os::boot::BootStageKind required[] {
            os::boot::BootStageKind::first_stage,
            os::boot::BootStageKind::bootloader,
            os::boot::BootStageKind::kernel,
            os::boot::BootStageKind::configuration,
            os::boot::BootStageKind::root_filesystem,
        };
        for (const auto kind : required) {
            if (!state.stage_of_kind(kind)) {
                __builtin_trap();
            }
        }
    }

    // The stage table must be internally consistent: every index below the
    // count resolves, and no kind appears twice.
    if (state.stage_count() > os::boot::max_boot_stages) {
        __builtin_trap();
    }
    for (std::size_t left = 0U; left < state.stage_count(); ++left) {
        auto first = state.stage(left);
        if (!first) {
            __builtin_trap();
        }
        for (std::size_t right = left + 1U; right < state.stage_count(); ++right) {
            auto second = state.stage(right);
            if (!second) {
                __builtin_trap();
            }
            if (first.value().kind == second.value().kind) {
                __builtin_trap();
            }
        }
    }

    // Re-encoding an accepted state must reproduce a record that parses to the
    // same state. A divergence means parse() and encode() disagree about the
    // format, which is how a record that means one thing to the verifier comes
    // to mean another to a consumer.
    std::byte round_trip[os::boot::max_boot_state_bytes] {};
    auto encoded = os::boot::encode_boot_state_v1(
        state, os::core::MutableByteSpan{round_trip, os::boot::max_boot_state_bytes});
    if (!encoded) {
        __builtin_trap();
    }
    auto reparsed = os::boot::parse_boot_state_v1(
        os::core::ByteSpan{round_trip, encoded.value()});
    if (!reparsed) {
        __builtin_trap();
    }
    if (reparsed.value().verified() != state.verified() ||
        reparsed.value().lifecycle() != state.lifecycle() ||
        reparsed.value().security_version() != state.security_version() ||
        reparsed.value().stage_count() != state.stage_count()) {
        __builtin_trap();
    }

    return 0;
}
