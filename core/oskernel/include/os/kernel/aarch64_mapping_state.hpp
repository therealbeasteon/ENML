#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <os/core/error.hpp>
#include <os/core/panic.hpp>
#include <os/core/result.hpp>
#include <os/kernel/aarch64_page_tables.hpp>

namespace os::kernel::aarch64 {

inline constexpr std::size_t max_native_mappings = 64U;
inline constexpr std::size_t max_native_physical_mappings = 256U;

struct NativeMapping final {
    std::uint64_t virtual_base {0ULL};
    std::uint64_t physical_base {0ULL};
    std::uint64_t length {0ULL};
    MachinePermissions permissions {MachinePermissions::read};
    MachineMemoryKind kind {MachineMemoryKind::normal};
    bool kernel_stack {false};
    bool user_stack {false};
    bool user_accessible {false};
    bool occupied {false};
};

struct NativePhysicalMapping final {
    const void* owner {nullptr};
    std::uint64_t physical_base {0ULL};
    std::uint64_t length {0ULL};
    MachinePermissions permissions {MachinePermissions::read};
    bool occupied {false};
};

struct NativePhysicalLedger final {
    std::array<NativePhysicalMapping, max_native_physical_mappings> mappings {};
    std::size_t occupied {0U};
};

class NativeAddressSpaceState final {
public:
    [[nodiscard]] os::core::Result<void> bind(
        NativePhysicalLedger& ledger,
        EarlyStage1Builder& builder) noexcept {
        if (ledger_ != nullptr || builder_ != nullptr) {
            return error(machine_errors::address_space_already_bound);
        }
        if (builder.root_physical() == 0ULL) {
            return error(machine_errors::address_space_unbound);
        }
        ledger_ = &ledger;
        builder_ = &builder;
        return {};
    }

    [[nodiscard]] os::core::Result<void> map(
        std::uint64_t virtual_base,
        std::uint64_t physical_base,
        std::uint64_t length,
        MachinePermissions permissions,
        MachineMemoryKind kind,
        bool kernel_stack = false) noexcept {
        return map_impl(
            virtual_base, physical_base, length, permissions, kind,
            kernel_stack, false, false);
    }

    [[nodiscard]] os::core::Result<void> map_user(
        std::uint64_t virtual_base,
        std::uint64_t physical_base,
        std::uint64_t length,
        MachinePermissions permissions) noexcept {
        return map_impl(
            virtual_base, physical_base, length, permissions,
            MachineMemoryKind::normal, false, false, true);
    }

    [[nodiscard]] os::core::Result<void> map_kernel_stack(
        std::uint64_t virtual_base,
        std::uint64_t physical_base,
        std::uint64_t length) noexcept {
        auto guard = require_guard_page(virtual_base);
        if (!guard) return guard.error();
        return map_impl(
            virtual_base, physical_base, length,
            MachinePermissions::read_write, MachineMemoryKind::normal,
            true, false, false);
    }

    [[nodiscard]] os::core::Result<void> map_user_stack(
        std::uint64_t virtual_base,
        std::uint64_t physical_base,
        std::uint64_t length) noexcept {
        auto guard = require_guard_page(virtual_base);
        if (!guard) return guard.error();
        return map_impl(
            virtual_base, physical_base, length,
            MachinePermissions::read_write, MachineMemoryKind::normal,
            false, true, true);
    }

    [[nodiscard]] bool valid_kernel_stack_top(std::uint64_t stack_top) const noexcept {
        for (const auto& mapping : mappings_) {
            if (!mapping.occupied || !mapping.kernel_stack) continue;
            if (mapping.virtual_base <= UINT64_MAX - mapping.length &&
                mapping.virtual_base + mapping.length == stack_top) return true;
        }
        return false;
    }

    [[nodiscard]] bool valid_user_stack_top(std::uint64_t stack_top) const noexcept {
        for (const auto& mapping : mappings_) {
            if (!mapping.occupied || !mapping.user_stack || !mapping.user_accessible) continue;
            if (mapping.virtual_base <= UINT64_MAX - mapping.length &&
                mapping.virtual_base + mapping.length == stack_top) return true;
        }
        return false;
    }

    [[nodiscard]] bool valid_user_entry(std::uint64_t entry) const noexcept {
        for (const auto& mapping : mappings_) {
            if (!mapping.occupied || !mapping.user_accessible ||
                mapping.permissions != MachinePermissions::read_execute) continue;
            if (entry >= mapping.virtual_base &&
                entry < mapping.virtual_base + mapping.length) return true;
        }
        return false;
    }

    [[nodiscard]] os::core::Result<NativeMapping> exact_mapping(
        std::uint64_t virtual_base,
        std::uint64_t length) const noexcept {
        if (ledger_ == nullptr || builder_ == nullptr) {
            return error(machine_errors::address_space_unbound);
        }
        for (const auto& mapping : mappings_) {
            if (mapping.occupied && mapping.virtual_base == virtual_base && mapping.length == length) {
                return mapping;
            }
        }
        return error(machine_errors::not_mapped);
    }

    [[nodiscard]] os::core::Result<void> retire_unmapped(
        std::uint64_t virtual_base,
        std::uint64_t length) noexcept {
        if (ledger_ == nullptr || builder_ == nullptr) {
            return error(machine_errors::address_space_unbound);
        }

        NativeMapping* local = nullptr;
        for (auto& mapping : mappings_) {
            if (mapping.occupied && mapping.virtual_base == virtual_base && mapping.length == length) {
                local = &mapping;
                break;
            }
        }
        if (local == nullptr) return error(machine_errors::not_mapped);

        NativePhysicalMapping* physical = nullptr;
        for (auto& mapping : ledger_->mappings) {
            if (!mapping.occupied || mapping.owner != this) continue;
            if (mapping.physical_base == local->physical_base &&
                mapping.length == local->length &&
                mapping.permissions == local->permissions) {
                physical = &mapping;
                break;
            }
        }
        if (physical == nullptr) return error(machine_errors::mapping_ledger_inconsistent);

        const auto pages = local->length / architectural_page_size;
        for (std::uint64_t page = 0ULL; page < pages; ++page) {
            auto still_mapped = builder_->mapped(
                local->virtual_base + page * architectural_page_size);
            if (!still_mapped) return still_mapped.error();
            if (still_mapped.value()) return error(machine_errors::mapping_ledger_inconsistent);
        }

        *physical = NativePhysicalMapping{};
        *local = NativeMapping{};
        --ledger_->occupied;
        --occupied_;
        return {};
    }

    [[nodiscard]] std::size_t mapping_count() const noexcept { return occupied_; }

private:
    std::array<NativeMapping, max_native_mappings> mappings_ {};
    std::size_t occupied_ {0U};
    NativePhysicalLedger* ledger_ {nullptr};
    EarlyStage1Builder* builder_ {nullptr};

    [[nodiscard]] os::core::Result<void> require_guard_page(
        std::uint64_t virtual_base) const noexcept {
        if (virtual_base < architectural_page_size) {
            return error(machine_errors::missing_guard_page);
        }
        const std::uint64_t guard = virtual_base - architectural_page_size;
        for (const auto& mapping : mappings_) {
            if (mapping.occupied && overlap(
                    mapping.virtual_base, mapping.length,
                    guard, architectural_page_size)) {
                return error(machine_errors::missing_guard_page);
            }
        }
        if (builder_ != nullptr) {
            auto guard_state = builder_->mapped(guard);
            if (!guard_state) return guard_state.error();
            if (guard_state.value()) return error(machine_errors::missing_guard_page);
        }
        return {};
    }

    [[nodiscard]] os::core::Result<void> map_impl(
        std::uint64_t virtual_base,
        std::uint64_t physical_base,
        std::uint64_t length,
        MachinePermissions permissions,
        MachineMemoryKind kind,
        bool kernel_stack,
        bool user_stack,
        bool user_accessible) noexcept {
        if (ledger_ == nullptr || builder_ == nullptr) {
            return error(machine_errors::address_space_unbound);
        }
        if (user_accessible && kind != MachineMemoryKind::normal) {
            return error(machine_errors::invalid_range);
        }
        if (user_stack && (!user_accessible || permissions != MachinePermissions::read_write)) {
            return error(machine_errors::invalid_range);
        }
        if (length == 0ULL || !page_aligned(length) ||
            !page_aligned(virtual_base) || !page_aligned(physical_base) ||
            !stage1_virtual_address(virtual_base) ||
            !stage1_physical_address(physical_base) ||
            virtual_base > UINT64_MAX - (length - 1ULL) ||
            physical_base > UINT64_MAX - (length - 1ULL) ||
            !stage1_virtual_address(virtual_base + length - architectural_page_size) ||
            !stage1_physical_address(physical_base + length - architectural_page_size)) {
            return error(machine_errors::invalid_range);
        }

        for (const auto& existing : mappings_) {
            if (existing.occupied && overlap(
                    existing.virtual_base, existing.length, virtual_base, length)) {
                return error(machine_errors::already_mapped);
            }
        }

        for (const auto& existing : ledger_->mappings) {
            if (!existing.occupied || !overlap(
                    existing.physical_base, existing.length, physical_base, length)) continue;
            if ((writable(existing.permissions) && executable(permissions)) ||
                (executable(existing.permissions) && writable(permissions))) {
                return error(machine_errors::writable_executable_alias);
            }
        }

        auto* local_slot = free_local();
        auto* physical_slot = free_physical();
        if (local_slot == nullptr || physical_slot == nullptr) {
            return error(machine_errors::exhausted);
        }

        const std::uint64_t page_count = length / architectural_page_size;
        if (required_table_pages(virtual_base, page_count) > builder_->remaining_table_pages()) {
            return error(machine_errors::exhausted);
        }

        for (std::uint64_t page = 0ULL; page < page_count; ++page) {
            auto state = builder_->mapped(virtual_base + page * architectural_page_size);
            if (!state) return state.error();
            if (state.value()) return error(machine_errors::already_mapped);
        }

        for (std::uint64_t page = 0ULL; page < page_count; ++page) {
            const auto va = virtual_base + page * architectural_page_size;
            const auto pa = physical_base + page * architectural_page_size;
            auto mapped = user_accessible
                ? builder_->map_user_page(va, pa, permissions)
                : builder_->map_page(va, pa, permissions, kind);
            if (!mapped) os::core::invariant_violated();
        }

        *local_slot = NativeMapping{
            virtual_base, physical_base, length, permissions, kind,
            kernel_stack, user_stack, user_accessible, true};
        *physical_slot = NativePhysicalMapping{
            this, physical_base, length, permissions, true};
        ++occupied_;
        ++ledger_->occupied;
        return {};
    }

    [[nodiscard]] static constexpr os::core::Error error(std::uint32_t code) noexcept {
        return os::core::make_error(os::core::ErrorDomain::kernel, code);
    }
    [[nodiscard]] static constexpr bool writable(MachinePermissions value) noexcept {
        return value == MachinePermissions::read_write;
    }
    [[nodiscard]] static constexpr bool executable(MachinePermissions value) noexcept {
        return value == MachinePermissions::read_execute;
    }
    [[nodiscard]] static constexpr bool overlap(
        std::uint64_t a_base, std::uint64_t a_length,
        std::uint64_t b_base, std::uint64_t b_length) noexcept {
        return a_base < b_base + b_length && b_base < a_base + a_length;
    }

    [[nodiscard]] NativeMapping* free_local() noexcept {
        for (auto& mapping : mappings_) if (!mapping.occupied) return &mapping;
        return nullptr;
    }
    [[nodiscard]] NativePhysicalMapping* free_physical() noexcept {
        for (auto& mapping : ledger_->mappings) if (!mapping.occupied) return &mapping;
        return nullptr;
    }

    [[nodiscard]] static std::size_t required_table_pages(
        std::uint64_t virtual_base,
        std::uint64_t page_count) noexcept {
        std::size_t l1_regions = 0U;
        std::size_t l2_regions = 0U;
        std::uint16_t previous_l1 = 0xFFFFU;
        std::uint16_t previous_l2 = 0xFFFFU;
        for (std::uint64_t page = 0ULL; page < page_count; ++page) {
            const auto va = virtual_base + page * architectural_page_size;
            const auto l1 = level1_index(va);
            const auto l2 = level2_index(va);
            if (l1 != previous_l1) {
                ++l1_regions;
                previous_l1 = l1;
                previous_l2 = 0xFFFFU;
            }
            if (l2 != previous_l2) {
                ++l2_regions;
                previous_l2 = l2;
            }
        }
        return l1_regions + l2_regions;
    }
};

} // namespace os::kernel::aarch64
