#pragma once

#include <cstdint>

namespace os::kernel {

namespace aarch64 {
class TranslationRootSealer;
}

// A sealed translation root is an execution capability, not a raw address.
// Only the architecture sealer may mint one after a page-table builder has
// crossed its building -> sealed transition. Process/scheduler code can carry
// the token but cannot manufacture an executable memory universe from an
// arbitrary physical page.
//
// It carries two things, and the second is the whole of
// docs/M7_12_ENTRY_BINDING.md: the root page, and the one virtual address at
// which execution in this space may begin. They are bound together in the same
// one-way transition because they answer the same question - what it means for
// this space to be executable - and because binding the entry here is what
// makes it unchangeable afterwards. There is no path out of sealed state and no
// public constructor, so a principal holding a space cannot decide where a
// thread admitted to it starts.
//
// That matters because the loader and the image are different principals.
// A process manager holds an address space containing code it did not write
// and is not trusted to have written; if the entry were an argument to
// thread_create, it could enter signed code past its own initialisation while
// the content digest still verified. Every other system Cookie references -
// POSIX clone, seL4_TCB_WriteRegisters - takes the entry from the creator,
// because in those systems the creator is assumed to be the author.
class SealedTranslationRoot final {
public:
    constexpr SealedTranslationRoot() noexcept = default;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return root_physical_ != 0ULL && entry_ != 0ULL;
    }

    [[nodiscard]] constexpr std::uint64_t root_physical() const noexcept {
        return root_physical_;
    }

    // Where a thread admitted to this space begins. Not checked against what is
    // mapped, deliberately: an entry that is unmapped or not executable takes
    // an instruction abort and dies through the fault path, and a second weaker
    // copy of that check here is how the two come to disagree.
    [[nodiscard]] constexpr std::uint64_t entry() const noexcept { return entry_; }

    [[nodiscard]] friend constexpr bool operator==(
        const SealedTranslationRoot&,
        const SealedTranslationRoot&) = default;

private:
    constexpr SealedTranslationRoot(std::uint64_t root_physical, std::uint64_t entry) noexcept
        : root_physical_(root_physical), entry_(entry) {}

    std::uint64_t root_physical_ {0ULL};
    std::uint64_t entry_ {0ULL};

    friend class aarch64::TranslationRootSealer;
};

} // namespace os::kernel
