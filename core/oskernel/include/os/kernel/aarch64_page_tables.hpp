#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/core/result.hpp>
#include <os/kernel/aarch64_translation.hpp>

namespace os::kernel::aarch64 {

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
        if (!valid()) {
            next_ = 0ULL;
            end_ = 0ULL;
            return os::core::make_error(
                os::core::ErrorDomain::kernel,
                machine_errors::invalid_range);
        }
        return {};
    }

    [[nodiscard]] bool valid() const noexcept {
        return page_aligned(next_) && page_aligned(end_) && next_ < end_ &&
               stage1_physical_address(next_) &&
               (end_ - architectural_page_size) <= page_address_mask;
    }

    [[nodiscard]] os::core::Result<std::uint64_t> allocate_page() noexcept {
        if (!valid() || next_ > end_ - architectural_page_size) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::exhausted);
        }
        const auto page = next_;
        next_ += architectural_page_size;
        return page;
    }

    [[nodiscard]] std::uint64_t next() const noexcept { return next_; }
    [[nodiscard]] std::size_t remaining_pages() const noexcept {
        if (!valid()) return 0U;
        return static_cast<std::size_t>((end_ - next_) / architectural_page_size);
    }

private:
    std::uint64_t next_ {0ULL};
    std::uint64_t end_ {0ULL};
};

namespace translation_root_errors {
inline constexpr std::uint32_t sealed = 100U;
inline constexpr std::uint32_t not_sealed = 101U;
inline constexpr std::uint32_t retiring = 102U;
inline constexpr std::uint32_t wrong_region = 103U;
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
