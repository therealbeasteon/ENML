#pragma once

#include <os/core/result.hpp>
#include <os/kernel/aarch64_page_tables.hpp>
#include <os/kernel/translation_root.hpp>

namespace os::kernel::aarch64 {

// Separates construction authority from execution authority. A caller may own a
// mutable page-table builder while assembling a process, but it receives an
// executable root token only after the builder irreversibly enters sealed state.
//
// The entry address is supplied here and nowhere else, which is
// docs/M7_12_ENTRY_BINDING.md's mechanism: whoever is assembling the space
// declares where execution begins while it still holds construction authority,
// and loses the ability to change that answer in the same transition that gains
// it the ability to run the space at all.
class TranslationRootSealer final {
public:
    [[nodiscard]] static os::core::Result<SealedTranslationRoot> seal(
        EarlyStage1Builder& builder,
        std::uint64_t entry) noexcept {
        // Checked before sealing, because sealing is irreversible: a builder
        // burned on a rejected entry could never be sealed again, so the
        // caller would be punished for a mistake it could have fixed.
        //
        // Three conditions, and the alignment one is not housekeeping. A64
        // instructions are four bytes and four-byte aligned, so an unaligned
        // entry names a point inside an instruction - which is precisely the
        // "enter signed code somewhere it did not intend" move this binding
        // exists to refuse, arriving by the smallest possible offset.
        if (entry == 0ULL || (entry & 0x3ULL) != 0ULL ||
            !user_stage1_virtual_address(entry)) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel,
                translation_root_errors::invalid_entry);
        }
        auto sealed = builder.seal();
        if (!sealed) return sealed.error();
        if (!builder.executable_process_root()) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel,
                translation_root_errors::not_sealed);
        }
        return SealedTranslationRoot{builder.root_physical(), entry};
    }
};

} // namespace os::kernel::aarch64
