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
// One slot per declared range of kernel state, and the table is per *ledger*
// rather than per space - so every space sharing boot_physical_ledger draws
// from this one number.
//
// Raised from 16 for M7.12, and the reason is a capacity fact rather than a
// rule being relaxed. Each page donated to a post-boot address space takes a
// slot, because reserving is per page - and a space a thread will actually run
// in has to replay the whole kernel mapping manifest into its own root, so
// long as Cookie translates through TTBR0 only. That is tens of table pages,
// where 16 was chosen at a time when nothing had donated any. Nothing about
// what a reservation *means* changes with the count.
//
// It shrinks when M7.7's TTBR1 split lands and a process root stops having to
// carry the kernel's mappings at all.
inline constexpr std::size_t max_native_physical_reservations = 64U;

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
    bool user_accessible {false};
    bool occupied {false};
};

// How tightly a reserved range's writers are constrained. Both kinds forbid
// the same two things - any EL0 translation at all, and any executable one -
// and differ only in who may hold a writable kernel translation.
enum class PhysicalReservationKind : std::uint8_t {
    // Exactly one address space may write it. The page-table arena: the kernel
    // edits its own tables through one translation, and a second writable one
    // is an alias with no legitimate use.
    kernel_object = 1U,
    // Any bound address space may write it. Not a weaker intention, a forced
    // one: Cookie translates through TTBR0 only, so EL1 executes under whatever
    // process root is installed, and the kernel's own globals and stack must be
    // writable in *every* space or the kernel cannot run once a process is
    // scheduled. Owner-write-only becomes achievable for these ranges when M7.7
    // splits the kernel domain into TTBR1 (see
    // aarch64_kernel_translation_domain.hpp), and not before.
    kernel_private = 2U,
};

// A physical range whose contents are kernel state rather than a process's
// data - the page-table arena, the kernel's writable image and its stack, and
// later any object derived from memory a process holds authority over. Three
// rules follow from that and are enforced wherever a mapping is created:
//
//   * nobody may reach it from EL0, owner included. A process that can write
//     its own translation tables has no address space, and one that can read
//     them learns the physical layout of every other.
//   * nobody may execute it. Kernel state is not code, and these ranges are
//     chosen by boot-time discovery or by the linker's data sections, never as
//     something to branch to.
//   * writable by the owner only, or by any bound space, per the kind above.
//
// The owner is an address space rather than a process because that is the
// granularity the ledger already records. This is the field M7.11's design
// adds to a structure the kernel already walks on every map, rather than the
// separate derivation tree it declined to copy - a second source of truth
// about physical memory can disagree with the first.
struct NativePhysicalReservation final {
    const void* owner {nullptr};
    std::uint64_t physical_base {0ULL};
    std::uint64_t length {0ULL};
    PhysicalReservationKind kind {PhysicalReservationKind::kernel_object};
    bool occupied {false};
};

struct NativePhysicalLedger final {
    std::array<NativePhysicalMapping, max_native_physical_mappings> mappings {};
    std::array<NativePhysicalReservation, max_native_physical_reservations> reservations {};
    std::size_t occupied {0U};
    std::size_t reserved {0U};
};

class NativeAddressSpaceState final {
public:
    [[nodiscard]] os::core::Result<void> bind(
        NativePhysicalLedger& ledger,
        EarlyStage1Builder& builder) noexcept {
        if (ledger_ != nullptr || builder_ != nullptr) {
            return error(machine_errors::address_space_already_bound);
        }
        // An uninitialized builder is accepted, and this used to require a root.
        // The old requirement encoded boot's order - fill the arena, build the
        // root, bind - and that order is impossible after boot, because the
        // pages a root is built from are donated by a caller and donation needs
        // the space already bound to reserve through. Requiring a root here
        // made creating an address space depend on itself.
        //
        // Safe to relax because a rootless builder already fails closed
        // everywhere it matters: install_leaf and leaf_pointer both return
        // address_space_unbound when root_ is zero, so every map, unmap and
        // query through this space errors until the root exists. Only
        // reserve_physical works in that window, which is exactly what donation
        // needs and nothing more.
        ledger_ = &ledger;
        builder_ = &builder;
        return {};
    }

    // Declares a physical range to hold kernel state, owned by this address
    // space. Intended to run before the range is mapped at all, but it does not
    // assume so: a reservation made after the fact must not silently bless a
    // translation it would have refused, so every live mapping of the range is
    // re-checked against the rule about to start applying.
    [[nodiscard]] os::core::Result<void> reserve_physical(
        std::uint64_t physical_base,
        std::uint64_t length,
        PhysicalReservationKind kind) noexcept {
        if (ledger_ == nullptr || builder_ == nullptr) {
            return error(machine_errors::address_space_unbound);
        }
        if (length == 0ULL || !page_aligned(length) || !page_aligned(physical_base) ||
            !stage1_physical_address(physical_base) ||
            physical_base > UINT64_MAX - (length - 1ULL) ||
            !stage1_physical_address(physical_base + length - architectural_page_size)) {
            return error(machine_errors::invalid_range);
        }

        for (const auto& existing : ledger_->reservations) {
            if (existing.occupied && overlap(
                    existing.physical_base, existing.length, physical_base, length)) {
                return error(machine_errors::already_mapped);
            }
        }
        for (const auto& existing : ledger_->mappings) {
            if (!existing.occupied || !overlap(
                    existing.physical_base, existing.length, physical_base, length)) continue;
            if (forbidden_by_reservation(
                    kind, this, existing.owner,
                    existing.permissions, existing.user_accessible)) {
                return error(machine_errors::kernel_object_alias);
            }
        }

        auto* slot = free_reservation();
        if (slot == nullptr) return error(machine_errors::exhausted);
        *slot = NativePhysicalReservation{this, physical_base, length, kind, true};
        ++ledger_->reserved;
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

    // Demand-paged backing for a space that may already be executing.
    //
    // Identical to map_user in every check it performs - W^X across the
    // ledger, the reservation rules, the guard-page and table-budget limits -
    // and different in exactly one respect: it reaches the builder through
    // back_absent_user_page, which a sealed space accepts and map_user_page
    // does not. Routing it through map_impl rather than around it is the
    // point; a backing path that skipped those checks would be a hole shaped
    // like a feature.
    [[nodiscard]] os::core::Result<void> map_user_backing(
        std::uint64_t virtual_base,
        std::uint64_t physical_base,
        std::uint64_t length,
        MachinePermissions permissions) noexcept {
        return map_impl(
            virtual_base, physical_base, length, permissions,
            MachineMemoryKind::normal, false, false, true, true);
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

    // Retires software authority only after the caller has cleared the hardware
    // descriptors and completed the architectural TLB invalidation sequence.
    // Every leaf is re-read here so ordering bugs cannot silently leave a valid
    // translation after the physical W^X ledger says the mapping is gone.
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

    // Teardown, in the three steps the hardware forces rather than one call.
    //
    // A space is released by repeatedly asking for a mapping, removing it
    // through the ordinary machine_unmap path - descriptor clear, TLBI, ledger
    // retirement - and asking again. That is deliberately slower than walking
    // the table directly and it is the point: bulk teardown that reimplements
    // unmapping is a second place for the break-before-make ordering to be got
    // wrong, and the M7.5e justification for that ordering says a stale TLB
    // entry after an unmap is a use-after-free with hardware caching it.
    [[nodiscard]] os::core::Result<NativeMapping> any_mapping() const noexcept {
        if (ledger_ == nullptr || builder_ == nullptr) {
            return error(machine_errors::address_space_unbound);
        }
        for (const auto& mapping : mappings_) {
            if (mapping.occupied) return mapping;
        }
        return error(machine_errors::not_mapped);
    }

    // One reservation this space owns, for a caller tearing them down. Mirrors
    // any_mapping() and exists for the same reason: the caller has work to do
    // per range - zeroing it - that this header must not do itself, because
    // writing memory is a machine operation and everything here is bookkeeping.
    [[nodiscard]] os::core::Result<NativePhysicalReservation> any_reservation() const noexcept {
        if (ledger_ == nullptr) return error(machine_errors::address_space_unbound);
        for (const auto& entry : ledger_->reservations) {
            if (entry.occupied && entry.owner == this) return entry;
        }
        return error(machine_errors::not_mapped);
    }

    // Drops exactly one, so a caller can pair each drop with the zeroing of
    // that range and stop on the first failure with the rest still reserved.
    // release_reservations() below drops them all at once, which cannot be
    // paired with anything and would leave unzeroed ranges unreserved if the
    // caller failed part-way.
    [[nodiscard]] os::core::Result<void> release_one_reservation(
        std::uint64_t physical_base,
        std::uint64_t length) noexcept {
        if (ledger_ == nullptr) return error(machine_errors::address_space_unbound);
        for (auto& entry : ledger_->reservations) {
            if (!entry.occupied || entry.owner != this) continue;
            if (entry.physical_base != physical_base || entry.length != length) continue;
            entry = NativePhysicalReservation{};
            --ledger_->reserved;
            return {};
        }
        return error(machine_errors::not_mapped);
    }

    // Drops the reservations this space owns. Separate from unbind() because a
    // reservation outlives the mappings of the range it covers: the page-table
    // arena is reserved before anything maps it and must stay reserved until
    // the last translation of it is gone, or a teardown in progress would open
    // a window where the range is briefly mappable from EL0.
    [[nodiscard]] os::core::Result<void> release_reservations() noexcept {
        if (ledger_ == nullptr) return error(machine_errors::address_space_unbound);
        for (auto& entry : ledger_->reservations) {
            if (!entry.occupied || entry.owner != this) continue;
            entry = NativePhysicalReservation{};
            --ledger_->reserved;
        }
        return {};
    }

    // Final step. Refuses while anything this space owns is still recorded,
    // because an unbound space cannot be asked about later: the ledger entries
    // would keep an owner pointer to a state object nobody consults again, and
    // a future map of that physical range would find a W^X peer that no longer
    // exists. Fail closed instead of leaking authority.
    [[nodiscard]] os::core::Result<void> unbind() noexcept {
        if (ledger_ == nullptr || builder_ == nullptr) {
            return error(machine_errors::address_space_unbound);
        }
        if (occupied_ != 0U) return error(machine_errors::mapping_ledger_inconsistent);
        for (const auto& entry : ledger_->mappings) {
            if (entry.occupied && entry.owner == this) {
                return error(machine_errors::mapping_ledger_inconsistent);
            }
        }
        for (const auto& entry : ledger_->reservations) {
            if (entry.occupied && entry.owner == this) {
                return error(machine_errors::mapping_ledger_inconsistent);
            }
        }
        ledger_ = nullptr;
        builder_ = nullptr;
        return {};
    }

    [[nodiscard]] bool bound() const noexcept {
        return ledger_ != nullptr && builder_ != nullptr;
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
        bool user_accessible,
        bool backing = false) noexcept {
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

        for (const auto& reservation : ledger_->reservations) {
            if (!reservation.occupied || !overlap(
                    reservation.physical_base, reservation.length,
                    physical_base, length)) continue;
            if (forbidden_by_reservation(
                    reservation.kind, reservation.owner, this,
                    permissions, user_accessible)) {
                return error(machine_errors::kernel_object_alias);
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
            auto mapped = backing
                ? builder_->back_absent_user_page(va, pa, permissions)
                : (user_accessible
                    ? builder_->map_user_page(va, pa, permissions)
                    : builder_->map_page(va, pa, permissions, kind));
            if (!mapped) os::core::invariant_violated();
        }

        *local_slot = NativeMapping{
            virtual_base, physical_base, length, permissions, kind,
            kernel_stack, user_stack, user_accessible, true};
        *physical_slot = NativePhysicalMapping{
            this, physical_base, length, permissions, user_accessible, true};
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
    // The one statement of what a reservation forbids, so the map-time check
    // and the reserve-time re-check cannot drift apart. Both directions of the
    // same question: at map time the reservation exists and the mapping is
    // proposed; at reserve time the mapping exists and the reservation is.
    [[nodiscard]] static constexpr bool forbidden_by_reservation(
        PhysicalReservationKind kind,
        const void* reservation_owner,
        const void* mapping_owner,
        MachinePermissions permissions,
        bool user_accessible) noexcept {
        return user_accessible || executable(permissions) ||
               (kind == PhysicalReservationKind::kernel_object &&
                writable(permissions) && mapping_owner != reservation_owner);
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
    [[nodiscard]] NativePhysicalReservation* free_reservation() noexcept {
        for (auto& entry : ledger_->reservations) if (!entry.occupied) return &entry;
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
