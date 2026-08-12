#pragma once

#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/core/result.hpp>
#include <os/kernel/aarch64_translation.hpp>

namespace os::kernel::aarch64 {

// Monotonic boot-only page allocator for translation tables. It owns a
// bootloader/machine-port supplied RAM interval and never frees or allocates
// outside it. The general physical allocator replaces it after boot.
class EarlyPageArena final {
public:
    EarlyPageArena(std::uint64_t begin, std::uint64_t end) noexcept
        : next_(begin), end_(end) {}

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

// Page-only stage-1 table builder used before the general VM subsystem exists.
// Table page physical addresses must be directly addressable by the boot CPU
// (MMU off or identity mapped). No block mappings are accepted, which keeps
// guard pages and permission transitions unambiguous during bring-up.
class EarlyStage1Builder final {
public:
    explicit EarlyStage1Builder(EarlyPageArena& arena) noexcept : arena_(&arena) {}

    [[nodiscard]] os::core::Result<std::uint64_t> initialize() noexcept {
        if (arena_ == nullptr || !arena_->valid()) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::invalid_range);
        }
        if (root_ != 0ULL) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::already_mapped);
        }
        auto page = arena_->allocate_page();
        if (!page) return page.error();
        zero_table(page.value());
        root_ = page.value();
        return root_;
    }

    [[nodiscard]] os::core::Result<void> map_page(
        std::uint64_t virtual_address,
        std::uint64_t physical_address,
        MachinePermissions permissions,
        MachineMemoryKind kind) noexcept {
        if (root_ == 0ULL) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::address_space_unbound);
        }
        if (!page_aligned(virtual_address) || !stage1_virtual_address(virtual_address) ||
            !stage1_physical_address(physical_address)) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::alignment);
        }
        const auto leaf = page_descriptor(physical_address, permissions, kind);
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

    [[nodiscard]] std::uint64_t root_physical() const noexcept { return root_; }

private:
    EarlyPageArena* arena_ {nullptr};
    std::uint64_t root_ {0ULL};

    [[nodiscard]] static std::uint64_t* table_pointer(std::uint64_t physical) noexcept {
        return reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(physical));
    }

    static void zero_table(std::uint64_t physical) noexcept {
        auto* table = table_pointer(physical);
        for (std::size_t i = 0U; i < table_entries; ++i) table[i] = 0ULL;
    }

    [[nodiscard]] os::core::Result<std::uint64_t>
    ensure_next_table(std::uint64_t& entry) noexcept {
        if ((entry & descriptor::valid) != 0ULL) {
            if ((entry & descriptor::table_or_page) == 0ULL) {
                return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::mapping_ledger_inconsistent);
            }
            const auto physical = entry & page_address_mask;
            if (!stage1_physical_address(physical)) {
                return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::mapping_ledger_inconsistent);
            }
            return physical;
        }
        auto page = arena_->allocate_page();
        if (!page) return page.error();
        zero_table(page.value());
        entry = table_descriptor(page.value());
        if (entry == 0ULL) {
            return os::core::make_error(os::core::ErrorDomain::kernel, machine_errors::invalid_range);
        }
        return page.value();
    }
};

} // namespace os::kernel::aarch64
