#include <os/kernel/aarch64_page_tables.hpp>

#include <array>
#include <cstdlib>
#include <cstdint>

namespace {
// Templated so a Result can be passed directly. Result's operator bool is
// explicit, which satisfies the contextual conversion in `!value` but not
// an implicit conversion to a bool parameter.
template <typename T>
void require(const T& value) { if (!static_cast<bool>(value)) std::abort(); }
}

int main() {
    using namespace os::kernel;
    using namespace os::kernel::aarch64;

    alignas(4096) std::array<std::byte, 20U * 4096U> storage{};
    const auto begin = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(storage.data()));
    const auto end = begin + storage.size();

    EarlyPageArena arena{begin, end};
    require(arena.valid());

    EarlyStage1Builder builder{arena};
    auto root = builder.initialize();
    require(static_cast<bool>(root));
    require(root.value() == begin);
    require(builder.region() == Stage1Region::lower);
    require(builder.lifecycle() == EarlyStage1Builder::Lifecycle::building);

    constexpr std::uint64_t va = 0x0000'0000'4000'0000ULL;
    constexpr std::uint64_t pa = 0x0000'0000'8000'0000ULL;
    auto before = builder.mapped(va);
    require(static_cast<bool>(before));
    require(!before.value());

    auto mapped = builder.map_page(
        va, pa, MachinePermissions::read_execute, MachineMemoryKind::normal);
    require(static_cast<bool>(mapped));

    auto lower_to_kernel = builder.map_page(
        kernel_virtual_base,
        pa + 0x10000ULL,
        MachinePermissions::read_write,
        MachineMemoryKind::normal);
    require(!lower_to_kernel);

    auto second = builder.map_page(
        va + 4096ULL,
        pa + 4096ULL,
        MachinePermissions::read_write,
        MachineMemoryKind::normal);
    require(static_cast<bool>(second));

    auto removed = builder.unmap_page(va);
    require(static_cast<bool>(removed));
    auto after_remove = builder.mapped(va);
    require(static_cast<bool>(after_remove));
    require(!after_remove.value());

    // Intermediate translation tables remain allocated in the monotonic early
    // regime; removing a leaf never fabricates reusable physical memory.
    const auto table_pages_after_remove = builder.remaining_table_pages();
    auto remove_again = builder.unmap_page(va);
    require(!remove_again);
    require(remove_again.error().code == machine_errors::not_mapped);

    auto remapped = builder.map_page(
        va, pa, MachinePermissions::read_execute, MachineMemoryKind::normal);
    require(static_cast<bool>(remapped));
    require(builder.remaining_table_pages() == table_pages_after_remove);

    constexpr std::uint64_t user_va = 0x0000'0000'1000'0000ULL;
    constexpr std::uint64_t user_pa = 0x0000'0000'8200'0000ULL;
    require(static_cast<bool>(builder.map_user_page(
        user_va, user_pa, MachinePermissions::read_execute)));

    auto bad_device_exec = builder.map_page(
        va + 8192ULL,
        0x0000'0000'0900'0000ULL,
        MachinePermissions::read_execute,
        MachineMemoryKind::device);
    require(!bad_device_exec);

    auto* l1 = reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(root.value()));
    const auto l2_pa = l1[level1_index(va)] & page_address_mask;
    auto* l2 = reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(l2_pa));
    const auto l3_pa = l2[level2_index(va)] & page_address_mask;
    auto* l3 = reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(l3_pa));
    const auto leaf = l3[level3_index(va)];
    require((leaf & page_address_mask) == pa);
    require((leaf & (3ULL << 6U)) == descriptor::ap_el1_ro_el0_none);
    require((leaf & descriptor::privileged_execute_never) == 0ULL);
    require((leaf & descriptor::unprivileged_execute_never) != 0ULL);
    // Checked on the leaf the builder actually installed, not only on what the
    // descriptor helper returns: a global entry is one the ASID-tagged switch
    // cannot separate from another address space, and this is the value the
    // walker will read.
    require((leaf & descriptor::not_global) != 0ULL);

    const auto user_l2_pa = l1[level1_index(user_va)] & page_address_mask;
    auto* user_l2 = reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(user_l2_pa));
    const auto user_l3_pa = user_l2[level2_index(user_va)] & page_address_mask;
    auto* user_l3 = reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(user_l3_pa));
    const auto user_leaf = user_l3[level3_index(user_va)];
    require((user_leaf & page_address_mask) == user_pa);
    require((user_leaf & (3ULL << 6U)) == descriptor::ap_el1_ro_el0_ro);
    require((user_leaf & descriptor::privileged_execute_never) != 0ULL);
    require((user_leaf & descriptor::unprivileged_execute_never) == 0ULL);
    require((user_leaf & descriptor::not_global) != 0ULL);

    // A process translation root becomes immutable before it is executable.
    require(static_cast<bool>(builder.seal()));
    require(builder.executable_process_root());
    require(!builder.sealed_kernel_root());
    require(!builder.map_user_page(
        user_va + architectural_page_size,
        user_pa + architectural_page_size,
        MachinePermissions::read_write));
    require(!builder.unmap_page(user_va));

    EarlyStage1Builder other{arena};
    auto other_root = other.initialize();
    require(static_cast<bool>(other_root));
    require(other_root.value() != root.value());
    constexpr std::uint64_t other_user_pa = 0x0000'0000'8300'0000ULL;
    require(static_cast<bool>(other.map_user_page(
        user_va,
        other_user_pa,
        MachinePermissions::read_execute)));
    require(static_cast<bool>(other.seal()));
    require(other.executable_process_root());

    auto* other_l1 = reinterpret_cast<std::uint64_t*>(
        static_cast<std::uintptr_t>(other_root.value()));
    const auto other_l2_pa = other_l1[level1_index(user_va)] & page_address_mask;
    auto* other_l2 = reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(other_l2_pa));
    const auto other_l3_pa = other_l2[level2_index(user_va)] & page_address_mask;
    auto* other_l3 = reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(other_l3_pa));
    require((other_l3[level3_index(user_va)] & page_address_mask) == other_user_pa);
    require((user_l3[level3_index(user_va)] & page_address_mask) == user_pa);

    EarlyStage1Builder kernel_builder{arena, Stage1Region::upper};
    auto kernel_root = kernel_builder.initialize();
    require(static_cast<bool>(kernel_root));
    require(kernel_builder.region() == Stage1Region::upper);
    constexpr std::uint64_t kernel_va = kernel_virtual_base + 0x0020'0000ULL;
    constexpr std::uint64_t kernel_pa = 0x0000'0000'8400'0000ULL;
    require(static_cast<bool>(kernel_builder.map_page(
        kernel_va,
        kernel_pa,
        MachinePermissions::read_execute,
        MachineMemoryKind::normal)));
    require(!kernel_builder.map_page(
        user_va,
        kernel_pa + architectural_page_size,
        MachinePermissions::read_write,
        MachineMemoryKind::normal));
    auto upper_user_attempt = kernel_builder.map_user_page(
        kernel_va + architectural_page_size,
        kernel_pa + architectural_page_size,
        MachinePermissions::read_write);
    require(!upper_user_attempt);
    require(upper_user_attempt.error().code == translation_root_errors::wrong_region);
    require(static_cast<bool>(kernel_builder.seal()));
    require(kernel_builder.sealed_kernel_root());
    require(!kernel_builder.executable_process_root());

    auto* kernel_l1 = reinterpret_cast<std::uint64_t*>(
        static_cast<std::uintptr_t>(kernel_root.value()));
    const auto kernel_l2_pa = kernel_l1[level1_index(kernel_va)] & page_address_mask;
    auto* kernel_l2 = reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(kernel_l2_pa));
    const auto kernel_l3_pa = kernel_l2[level2_index(kernel_va)] & page_address_mask;
    auto* kernel_l3 = reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(kernel_l3_pa));
    const auto kernel_leaf = kernel_l3[level3_index(kernel_va)];
    require((kernel_leaf & page_address_mask) == kernel_pa);
    require((kernel_leaf & (3ULL << 6U)) == descriptor::ap_el1_ro_el0_none);
    require((kernel_leaf & descriptor::not_global) != 0ULL);

    // Teardown is an explicit lifecycle transition. Only after begin_retire()
    // can leaf mappings be destructively removed.
    require(static_cast<bool>(builder.begin_retire()));
    require(builder.lifecycle() == EarlyStage1Builder::Lifecycle::retiring);
    require(static_cast<bool>(builder.unmap_page(user_va)));
    auto retired_user = builder.mapped(user_va);
    require(retired_user && !retired_user.value());

    // ---------------------------------------------------------------------
    // Donated pages: the post-boot source, and a donation-only arena.
    // ---------------------------------------------------------------------
    {
        alignas(4096) std::array<std::byte, 4U * 4096U> donations{};
        const auto base = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(donations.data()));

        // No bump range at all. This is the shape after boot: nothing was
        // planned for this space before any process existed.
        EarlyPageArena donated{};
        require(!donated.valid());
        require(donated.remaining_pages() == 0U);
        require(!donated.allocate_page());

        require(static_cast<bool>(donated.donate(base)));
        require(donated.valid());
        require(donated.remaining_pages() == 1U);
        require(donated.donated_pages() == 1U);

        // The same page twice would be handed out twice and become two
        // different tables at once.
        auto twice = donated.donate(base);
        require(!twice);
        require(twice.error().code == machine_errors::already_mapped);

        require(static_cast<bool>(donated.donate(base + 4096ULL)));
        auto taken = donated.allocate_page();
        require(static_cast<bool>(taken));
        require(taken.value() == base + 4096ULL);
        require(donated.remaining_pages() == 1U);
        auto taken_again = donated.allocate_page();
        require(static_cast<bool>(taken_again));
        require(taken_again.value() == base);
        require(donated.remaining_pages() == 0U);
        require(!donated.allocate_page());

        // Unaligned and null donations are refused.
        require(!donated.donate(base + 8ULL));
        require(!donated.donate(0ULL));

        // A donation-only arena really does build tables.
        EarlyPageArena live{};
        for (std::size_t i = 0U; i < 4U; ++i) {
            require(static_cast<bool>(live.donate(base + i * 4096ULL)));
        }
        EarlyStage1Builder donated_builder{live};
        auto donated_root = donated_builder.initialize();
        require(static_cast<bool>(donated_root));
        require(donated_root.value() != 0ULL);

        // Donations are preferred over a bump range, so boot's finite budget is
        // kept for the case that cannot be topped up.
        alignas(4096) std::array<std::byte, 2U * 4096U> bump{};
        const auto bump_base = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(bump.data()));
        EarlyPageArena mixed{bump_base, bump_base + bump.size()};
        require(mixed.remaining_pages() == 2U);
        require(static_cast<bool>(mixed.donate(base)));
        require(mixed.remaining_pages() == 3U);
        auto mixed_first = mixed.allocate_page();
        require(static_cast<bool>(mixed_first));
        require(mixed_first.value() == base);
        auto mixed_second = mixed.allocate_page();
        require(static_cast<bool>(mixed_second));
        require(mixed_second.value() == bump_base);

        // A page inside the bump range cannot also be donated - it would be
        // handed out by both paths.
        auto overlap = mixed.donate(bump_base + 4096ULL);
        require(!overlap);
        require(overlap.error().code == machine_errors::already_mapped);
    }

    // ---------------------------------------------------------------------
    // Backing a sealed space. Demand paging has to insert a translation into
    // a space that is already executing, and map_user_page refuses a sealed
    // builder - correctly, because sealing separates construction authority
    // from execution authority.
    //
    // back_absent_user_page is the narrow exception, and these check that it
    // is narrow: it may fill a hole and may not touch anything present. The
    // guarantee sealing has to make is that a live translation cannot change
    // under the CPU, not that a page can never be added.
    // ---------------------------------------------------------------------
    {
        alignas(4096) std::array<std::byte, 12U * 4096U> tables{};
        const auto base = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(tables.data()));
        EarlyPageArena sealed_arena{base, base + tables.size()};
        EarlyStage1Builder sealed_builder{sealed_arena};
        require(sealed_builder.initialize());

        constexpr std::uint64_t present = 0x0000'0000'2000'0000ULL;
        constexpr std::uint64_t absent = 0x0000'0000'2000'1000ULL;
        constexpr std::uint64_t frame_a = 0x0000'0000'9000'0000ULL;
        constexpr std::uint64_t frame_b = 0x0000'0000'9001'0000ULL;

        require(sealed_builder.map_user_page(present, frame_a, MachinePermissions::read_write));
        require(sealed_builder.seal());

        // The rule that made this necessary: a sealed space refuses ordinary
        // mapping outright.
        auto refused = sealed_builder.map_user_page(absent, frame_b, MachinePermissions::read_write);
        require(!refused);
        require(refused.error().code == translation_root_errors::sealed);

        // The hole can be filled.
        require(sealed_builder.back_absent_user_page(absent, frame_b, MachinePermissions::read_write));
        auto now_mapped = sealed_builder.mapped(absent);
        require(now_mapped);
        require(now_mapped.value());

        // What is already there cannot be changed, which is the whole
        // guarantee. If this ever succeeds, a live translation can be swapped
        // under a running thread and sealing means nothing.
        auto replaced = sealed_builder.back_absent_user_page(
            present, frame_b, MachinePermissions::read_write);
        require(!replaced);
        require(replaced.error().code == machine_errors::already_mapped);

        // Nor may a filled hole be refilled - it is present now like any other.
        auto refilled = sealed_builder.back_absent_user_page(
            absent, frame_a, MachinePermissions::read_write);
        require(!refilled);
        require(refilled.error().code == machine_errors::already_mapped);

        // A retiring space gains nothing. Unlike the sealed case there is no
        // operation that needs it to, and a space being torn down has no
        // business acquiring translations.
        require(sealed_builder.begin_retire());
        auto retiring = sealed_builder.back_absent_user_page(
            0x0000'0000'2000'2000ULL, frame_b, MachinePermissions::read_write);
        require(!retiring);
        require(retiring.error().code == translation_root_errors::retiring);
    }

    return 0;
}
