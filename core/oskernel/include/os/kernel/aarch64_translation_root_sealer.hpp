#pragma once

#include <os/core/result.hpp>
#include <os/kernel/aarch64_page_tables.hpp>
#include <os/kernel/translation_root.hpp>

namespace os::kernel::aarch64 {

// Separates construction authority from execution authority. A caller may own a
// mutable page-table builder while assembling a process, but it receives an
// executable root token only after the builder irreversibly enters sealed state.
class TranslationRootSealer final {
public:
    [[nodiscard]] static os::core::Result<SealedTranslationRoot> seal(
        EarlyStage1Builder& builder) noexcept {
        auto sealed = builder.seal();
        if (!sealed) return sealed.error();
        if (!builder.executable_process_root()) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel,
                translation_root_errors::not_sealed);
        }
        return SealedTranslationRoot{builder.root_physical()};
    }
};

} // namespace os::kernel::aarch64
