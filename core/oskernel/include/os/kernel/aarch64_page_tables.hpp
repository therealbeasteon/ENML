#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/core/result.hpp>
#include <os/kernel/aarch64_translation.hpp>

namespace os::kernel::aarch64 {

// How many pages one address space may hold for translation structures at a
// time. Fixed, like every other kernel table, because M7.11 decided the kernel
// has no dynamic allocator. A caller that needs more donates more as it maps;
// running out is an ordinary `exhausted` a caller resolves by giving another
// page, not a kernel failure it can do nothing about.
// Raised from 32 for M7.12: a space a thread runs in must replay the kernel
// mapping manifest into its own root, which costs tens of tables on top of the
// space's own mappings. A caller that needs more than this donates - and is
// refused with `exhausted`, which is a caller error the caller can fix rather
// than a kernel failure.
inline constexpr std::size_t max_donated_table_pages = 64U;

// Supplies the pages translation structures are built from.
//
// Two sources, and the difference is the whole of why address spaces can be
// created after boot at all:
//
//   * A contiguous bump range, from `plan_early_boot_memory`. Monotonic, never
//     reclaimed, correct for boot and for nothing else - there is no caller to
//     ask, because no process exists yet.
//   * Donated pages, handed over one at a time by a process that already holds
//     authority over them. This is M7.11's "the kernel does not need a pool
//     because the caller supplies the pages", made concrete.
//
// Donated pages are used first. Boot's bump range is a finite budget chosen
// before anything ran, so spending a donation in preference to it keeps the
// irreplaceable resource for the case that cannot be topped up.
//
// A donated page must already be unreachable from EL0 before it arrives here,
// or the donor keeps a writable mapping of the page tables it is about to be
// governed by - the hole the physical-reservation work closed. This class does
// not enforce that; it cannot see the ledger. `aarch64_donate_table_page` is
// the enforcement point, and reserving through `reserve_physical` is what makes
// it safe, because that call re-checks live mappings and refuses a range some
// process can still reach.
class EarlyPageArena final {
public:
    constexpr EarlyPageArena() noexcept = default;
    constexpr EarlyPageArena(std::uint64_t begin, std::uint64_t end) noexcept
        : next_(begin), end_(end) {}

    [[nodiscard]] os::core::Result<void> bind(
        std::uint64_t begin,
        std::uint64_t end) noexcept {
        if (next_ != 0ULL || end_ != 0ULL) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel,
                machine_errors::already_mapped);
        }
        next_ = begin;
        end_ = end;
        if (!bump_valid()) {
            next_ = 0ULL;
            end_ = 0ULL;
            return os::core::make_error(
                os::core::ErrorDomain::kernel,
                machine_errors::invalid_range);
        }
        return {};
    }

    // A donation-only arena has no bump range and is still usable. That is the
    // post-boot shape: nothing was planned for it before any process existed.
    [[nodiscard]] bool valid() const noexcept {
        return bump_valid() || donated_count_ != 0U;
    }

    [[nodiscard]] os::core::Result<void> donate(std::uint64_t physical) noexcept {
        if (!page_aligned(physical) || !stage1_physical_address(physical) ||
            physical == 0ULL) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel, machine_errors::invalid_range);
        }
        // A page donated twice would be handed out twice and become two
        // different tables at once. Cheap to check against a fixed array, and
        // the failure it prevents is a translation structure aliasing another.
        for (std::size_t i = 0U; i < donated_count_; ++i) {
            if (donated_[i] == physical) {
                return os::core::make_error(
                    os::core::ErrorDomain::kernel, machine_errors::already_mapped);
            }
        }
        if (bump_valid() && physical >= next_ && physical < end_) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel, machine_errors::already_mapped);
        }
        if (donated_count_ >= max_donated_table_pages) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel, machine_errors::exhausted);
        }
        donated_[donated_count_++] = physical;
        return {};
    }

    [[nodiscard]] os::core::Result<std::uint64_t> allocate_page() noexcept {
        if (donated_count_ != 0U) {
            return donated_[--donated_count_];
        }
        if (!bump_valid() || next_ > end_ - architectural_page_size) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::exhausted);
        }
        const auto page = next_;
        next_ += architectural_page_size;
        return page;
    }

    [[nodiscard]] std::uint64_t next() const noexcept { return next_; }
    [[nodiscard]] std::size_t donated_pages() const noexcept { return donated_count_; }
    [[nodiscard]] std::size_t remaining_pages() const noexcept {
        const std::size_t bump = bump_valid()
            ? static_cast<std::size_t>((end_ - next_) / architectural_page_size)
            : 0U;
        return bump + donated_count_;
    }

private:
    std::uint64_t next_ {0ULL};
    std::uint64_t end_ {0ULL};
    std::array<std::uint64_t, max_donated_table_pages> donated_ {};
    std::size_t donated_count_ {0U};

    [[nodiscard]] bool bump_valid() const noexcept {
        return page_aligned(next_) && page_aligned(end_) && next_ < end_ &&
               stage1_physical_address(next_) &&
               (end_ - architectural_page_size) <= page_address_mask;
    }
};

namespace translation_root_errors {
inline constexpr std::uint32_t sealed = 100U;
inline constexpr std::uint32_t not_sealed = 101U;
inline constexpr std::uint32_t retiring = 102U;
inline constexpr std::uint32_t wrong_region = 103U;
// Sealing was asked for an entry that no thread could ever begin at. See
// docs/M7_12_ENTRY_BINDING.md: the entry is bound in the seal because that is
// what makes it unchangeable, so a seal without a usable one would mint an
// executable root that nothing can enter.
inline constexpr std::uint32_t invalid_entry = 104U;
} // namespace translation_root_errors

enum class Stage1Region : std::uint8_t {
    lower = 0U,
    upper = 1U,
};

// Page-only stage-1 table builder used before the general VM subsystem exists.
// Intermediate table pages are monotonic in the early regime: leaf mappings can
// be removed, but empty L2/L3 table pages are not reclaimed until the general
// physical allocator owns page-table lifetime.
class EarlyStage1Builder final {
public:
    enum class Lifecycle : std::uint8_t {
        uninitialized = 0U,
        building = 1U,
        sealed = 2U,
        retiring = 3U,
    };

    constexpr EarlyStage1Builder() noexcept = default;
    explicit constexpr EarlyStage1Builder(
        EarlyPageArena& arena,
        Stage1Region region = Stage1Region::lower) noexcept
        : arena_(&arena), region_(region) {}

    [[nodiscard]] os::core::Result<void> attach_arena(
        EarlyPageArena& arena,
        Stage1Region region = Stage1Region::lower) noexcept {
        if (arena_ != nullptr || root_ != 0ULL || lifecycle_ != Lifecycle::uninitialized) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel,
                machine_errors::already_mapped);
        }
        if (!arena.valid()) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel,
                machine_errors::invalid_range);
        }
        arena_ = &arena;
        region_ = region;
        return {};
    }

    [[nodiscard]] os::core::Result<std::uint64_t> initialize() noexcept {
        if (arena_ == nullptr || !arena_->valid()) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::invalid_range);
        }
        if (root_ != 0ULL || lifecycle_ != Lifecycle::uninitialized) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::already_mapped);
        }
        auto page = arena_->allocate_page();
        if (!page) return page.error();
        zero_table(page.value());
        root_ = page.value();
        lifecycle_ = Lifecycle::building;
        return root_;
    }

    [[nodiscard]] os::core::Result<void> seal() noexcept {
        if (lifecycle_ == Lifecycle::sealed) {
            return os::core::make_error(os::core::ErrorDomain::kernel, translation_root_errors::sealed);
        }
        if (lifecycle_ == Lifecycle::retiring) {
            return os::core::make_error(os::core::ErrorDomain::kernel, translation_root_errors::retiring);
        }
        if (lifecycle_ != Lifecycle::building || root_ == 0ULL) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::address_space_unbound);
        }
        lifecycle_ = Lifecycle::sealed;
        return {};
    }

    [[nodiscard]] os::core::Result<void> begin_retire() noexcept {
        if (lifecycle_ == Lifecycle::retiring) {
            return os::core::make_error(os::core::ErrorDomain::kernel, translation_root_errors::retiring);
        }
        if (lifecycle_ != Lifecycle::sealed) {
            return os::core::make_error(os::core::ErrorDomain::kernel, translation_root_errors::not_sealed);
        }
        lifecycle_ = Lifecycle::retiring;
        return {};
    }

    [[nodiscard]] os::core::Result<void> map_page(
        std::uint64_t virtual_address,
        std::uint64_t physical_address,
        MachinePermissions permissions,
        MachineMemoryKind kind) noexcept {
        auto mutable_result = require_building();
        if (!mutable_result) return mutable_result.error();
        return install_leaf(
            virtual_address,
            page_descriptor(physical_address, permissions, kind));
    }

    [[nodiscard]] os::core::Result<void> map_user_page(
        std::uint64_t virtual_address,
        std::uint64_t physical_address,
        MachinePermissions permissions) noexcept {
        auto mutable_result = require_building();
        if (!mutable_result) return mutable_result.error();
        if (region_ != Stage1Region::lower) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel,
                translation_root_errors::wrong_region);
        }
        return install_leaf(
            virtual_address,
            user_page_descriptor(physical_address, permissions));
    }

    // Fills a translation that is absent, in a space that may already be
    // executing. This is what demand paging is, and without it a sealed space
    // could never receive backing at all.
    //
    // Sealing exists to separate construction authority from execution
    // authority - a caller must not hold a mutable builder for a space it can
    // also run. Permitting this does not weaken that, and the reason is one
    // line further down rather than an argument: install_leaf already refuses a
    // leaf that is valid, with `already_mapped`. So this can only ever fill a
    // hole. Nothing that a live translation resolves to can change under the
    // CPU through this path, which is the property the seal has to protect.
    //
    // Retiring is still refused. A space being torn down has no business
    // gaining translations, and unlike the sealed case there is no operation
    // that needs it to.
    [[nodiscard]] os::core::Result<void> back_absent_user_page(
        std::uint64_t virtual_address,
        std::uint64_t physical_address,
        MachinePermissions permissions) noexcept {
        auto backable = require_backable();
        if (!backable) return backable.error();
        if (region_ != Stage1Region::lower) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel,
                translation_root_errors::wrong_region);
        }
        return install_leaf(
            virtual_address,
            user_page_descriptor(physical_address, permissions));
    }

    [[nodiscard]] os::core::Result<bool> mapped(std::uint64_t virtual_address) const noexcept {
        auto leaf = leaf_pointer(virtual_address);
        if (!leaf) {
            if (leaf.error().code == machine_errors::not_mapped) return false;
            return leaf.error();
        }
        return ((*leaf.value()) & descriptor::valid) != 0ULL;
    }

    // Clears exactly one level-3 page descriptor. This deliberately performs no
    // TLBI: architectural invalidation belongs to the machine layer, which must
    // order descriptor writes and TLB invalidation with DSB/ISB. Separating the
    // table mutation from CPU invalidation makes that ordering explicit.
    [[nodiscard]] os::core::Result<void> unmap_page(std::uint64_t virtual_address) noexcept {
        if (lifecycle_ == Lifecycle::sealed) {
            return os::core::make_error(os::core::ErrorDomain::kernel, translation_root_errors::sealed);
        }
        if (lifecycle_ != Lifecycle::building && lifecycle_ != Lifecycle::retiring) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::address_space_unbound);
        }
        auto leaf = leaf_pointer(virtual_address);
        if (!leaf) return leaf.error();
        if (((*leaf.value()) & descriptor::valid) == 0ULL) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::not_mapped);
        }
        *leaf.value() = 0ULL;
        return {};
    }

    [[nodiscard]] std::size_t remaining_table_pages() const noexcept {
        return arena_ == nullptr ? 0U : arena_->remaining_pages();
    }

    [[nodiscard]] std::uint64_t root_physical() const noexcept { return root_; }
    [[nodiscard]] Lifecycle lifecycle() const noexcept { return lifecycle_; }
    [[nodiscard]] Stage1Region region() const noexcept { return region_; }
    [[nodiscard]] bool executable_process_root() const noexcept {
        return region_ == Stage1Region::lower &&
               root_ != 0ULL && lifecycle_ == Lifecycle::sealed;
    }
    [[nodiscard]] bool sealed_kernel_root() const noexcept {
        return region_ == Stage1Region::upper &&
               root_ != 0ULL && lifecycle_ == Lifecycle::sealed;
    }

private:
    EarlyPageArena* arena_ {nullptr};
    std::uint64_t root_ {0ULL};
    Lifecycle lifecycle_ {Lifecycle::uninitialized};
    Stage1Region region_ {Stage1Region::lower};

    [[nodiscard]] bool accepts_virtual_address(std::uint64_t virtual_address) const noexcept {
        return region_ == Stage1Region::lower
            ? user_stage1_virtual_address(virtual_address)
            : kernel_stage1_virtual_address(virtual_address);
    }

    // Building or sealed, but never retiring or uninitialized. The difference
    // from require_building is deliberate and narrow: only back_absent_user_page
    // uses this, and only because it cannot change a translation that exists.
    [[nodiscard]] os::core::Result<void> require_backable() const noexcept {
        if (lifecycle_ == Lifecycle::retiring) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel, translation_root_errors::retiring);
        }
        if (lifecycle_ != Lifecycle::building && lifecycle_ != Lifecycle::sealed) {
            return os::core::make_error(
                os::core::ErrorDomain::kernel, machine_errors::address_space_unbound);
        }
        return {};
    }

    [[nodiscard]] os::core::Result<void> require_building() const noexcept {
        if (lifecycle_ == Lifecycle::sealed) {
            return os::core::make_error(os::core::ErrorDomain::kernel, translation_root_errors::sealed);
        }
        if (lifecycle_ == Lifecycle::retiring) {
            return os::core::make_error(os::core::ErrorDomain::kernel, translation_root_errors::retiring);
        }
        if (lifecycle_ != Lifecycle::building) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::address_space_unbound);
        }
        return {};
    }

    [[nodiscard]] static std::uint64_t* table_pointer(std::uint64_t physical) noexcept {
        return reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(physical));
    }

    static void zero_table(std::uint64_t physical) noexcept {
        auto* table = table_pointer(physical);
        for (std::size_t i = 0U; i < table_entries; ++i) table[i] = 0ULL;
    }

    [[nodiscard]] os::core::Result<void> install_leaf(
        std::uint64_t virtual_address,
        std::uint64_t leaf) noexcept {
        if (root_ == 0ULL) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::address_space_unbound);
        }
        if (!page_aligned(virtual_address) || !accepts_virtual_address(virtual_address)) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::alignment);
        }
        if (leaf == 0ULL) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::invalid_range);
        }

        auto* l1 = table_pointer(root_);
        auto l2_pa = ensure_next_table(l1[level1_index(virtual_address)]);
        if (!l2_pa) return l2_pa.error();
        auto* l2 = table_pointer(l2_pa.value());
        auto l3_pa = ensure_next_table(l2[level2_index(virtual_address)]);
        if (!l3_pa) return l3_pa.error();
        auto* l3 = table_pointer(l3_pa.value());
        auto& entry = l3[level3_index(virtual_address)];
        if ((entry & descriptor::valid) != 0ULL) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::already_mapped);
        }
        entry = leaf;
        return {};
    }

    [[nodiscard]] os::core::Result<std::uint64_t*>
    leaf_pointer(std::uint64_t virtual_address) const noexcept {
        if (root_ == 0ULL) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::address_space_unbound);
        }
        if (!page_aligned(virtual_address) || !accepts_virtual_address(virtual_address)) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::alignment);
        }

        auto* l1 = table_pointer(root_);
        const auto l1_entry = l1[level1_index(virtual_address)];
        if ((l1_entry & descriptor::valid) == 0ULL) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::not_mapped);
        }
        if ((l1_entry & descriptor::table_or_page) == 0ULL) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::mapping_ledger_inconsistent);
        }
        const auto l2_pa = l1_entry & page_address_mask;
        if (!stage1_physical_address(l2_pa)) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::mapping_ledger_inconsistent);
        }

        auto* l2 = table_pointer(l2_pa);
        const auto l2_entry = l2[level2_index(virtual_address)];
        if ((l2_entry & descriptor::valid) == 0ULL) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::not_mapped);
        }
        if ((l2_entry & descriptor::table_or_page) == 0ULL) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::mapping_ledger_inconsistent);
        }
        const auto l3_pa = l2_entry & page_address_mask;
        if (!stage1_physical_address(l3_pa)) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::mapping_ledger_inconsistent);
        }

        auto* l3 = table_pointer(l3_pa);
        auto* leaf = &l3[level3_index(virtual_address)];
        if (((*leaf) & descriptor::valid) != 0ULL &&
            ((*leaf) & descriptor::table_or_page) == 0ULL) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::mapping_ledger_inconsistent);
        }
        return leaf;
    }

    [[nodiscard]] os::core::Result<std::uint64_t> ensure_next_table(
        std::uint64_t& entry) noexcept {
        if ((entry & descriptor::valid) != 0ULL) {
            if ((entry & descriptor::table_or_page) == 0ULL) {
                return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::mapping_ledger_inconsistent);
            }
            const auto existing = entry & page_address_mask;
            if (!stage1_physical_address(existing)) {
                return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::mapping_ledger_inconsistent);
            }
            return existing;
        }
        if (arena_ == nullptr) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::address_space_unbound);
        }
        auto page = arena_->allocate_page();
        if (!page) return page.error();
        zero_table(page.value());
        entry = page.value() | descriptor::valid | descriptor::table_or_page;
        return page.value();
    }
};

} // namespace os::kernel::aarch64
