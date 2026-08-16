#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <os/core/span.hpp>
#include <os/kernel/abi.hpp>
#include <os/kernel/aarch64_entry.hpp>
#include <os/kernel/aarch64_execution_universe.hpp>
#include <os/kernel/aarch64_gic_v3.hpp>
#include <os/kernel/aarch64_interrupt_syscall.hpp>
#include <os/kernel/aarch64_ipc_syscall.hpp>
#include <os/kernel/aarch64_kernel_mapping_manifest.hpp>
#include <os/kernel/aarch64_page_tables.hpp>
#include <os/kernel/aarch64_preemption.hpp>
#include <os/kernel/aarch64_translation.hpp>
#include <os/kernel/aarch64_translation_root_sealer.hpp>
#include <os/kernel/aarch64_user_access.hpp>
#include <os/kernel/address_space_epoch.hpp>
#include <os/kernel/arch_timer_discovery.hpp>
#include <os/kernel/boot_memory.hpp>
#include <os/kernel/boot_memory_plan.hpp>
#include <os/kernel/fault_delivery.hpp>
#include <os/kernel/fault_region.hpp>
#include <os/kernel/fdt.hpp>
#include <os/kernel/gic_v3_discovery.hpp>
#include <os/kernel/hardware_inventory.hpp>
#include <os/kernel/kernel.hpp>
#include <os/kernel/machine.hpp>
#include <os/kernel/machine_aarch64.hpp>
#include <os/kernel/process_translation.hpp>
#include <os/kernel/scheduler.hpp>
#include <os/kernel/user_access.hpp>

#if !defined(__aarch64__)
#error "aarch64_boot.cpp must only be compiled for AArch64"
#endif

extern "C" char __cookie_image_start[];
extern "C" char __cookie_image_end[];
extern "C" char __cookie_text_start[];
extern "C" char __cookie_text_end[];
extern "C" char __cookie_rodata_start[];
extern "C" char __cookie_rodata_end[];
extern "C" char __cookie_data_start[];
extern "C" char __bss_end[];

namespace {
using os::kernel::HardwareRange;
using os::kernel::MachineMemoryKind;
using os::kernel::MachinePermissions;

constexpr std::uint64_t page_size = os::kernel::aarch64::architectural_page_size;
constexpr std::size_t max_boot_dtb_bytes = 2U * 1024U * 1024U;
constexpr std::size_t early_page_table_pages = 128U;
// Backing for the throwaway identity map built before the device tree can be
// safely parsed - see build_early_identity_map's call site. It covers at most
// four ranges (the kernel image's three segments plus the DTB, up to
// max_boot_dtb_bytes), against early_page_table_pages' 128 for three full,
// discovered address spaces, so a quarter of that budget is generous margin
// rather than a matched size.
constexpr std::size_t early_identity_table_pages = 32U;
constexpr std::size_t runtime_stack_pages = 8U;
// Four table pages plus one page for the created space to map. See the
// selection site for why they are selected rather than carved from the boot
// plan's arena.
constexpr std::size_t dynamic_proof_pages = 7U;
// Of those, the ones the boot proof donates as translation tables. The next
// page is what it maps, and the last is kept back for the EL0 proof to name as
// its own space's root - separate pages throughout, so neither proof can pass
// because of state the other left behind.
constexpr std::size_t dynamic_donated_pages = 4U;
constexpr std::uint64_t dynamic_proof_virtual = 0x0000'0000'2000'0000ULL;
constexpr std::uint64_t runtime_stack_virtual = 0x0000'007F'FEF0'0000ULL;
constexpr std::uint64_t user_code_virtual = 0x0000'0000'1000'0000ULL;
constexpr std::uint64_t user_stack_virtual = 0x0000'0000'1001'0000ULL;
constexpr std::uint64_t ipc_exchange_offset = 0x100ULL;
constexpr std::uint64_t ipc_response_offset = 0x200ULL;
constexpr std::uint64_t ipc_client_exchange = user_stack_virtual + ipc_exchange_offset;
constexpr std::uint64_t ipc_server_exchange = user_stack_virtual + ipc_exchange_offset;
constexpr std::uint64_t ipc_server_response = user_stack_virtual + ipc_response_offset;
constexpr std::uint64_t ipc_entry_virtual = user_code_virtual + 8U * sizeof(std::uint32_t);
constexpr std::size_t ipc_request_size = 6U;
constexpr std::size_t ipc_response_size = 2U;
constexpr std::uint64_t syscall_return_cookie = 0xC00CULL;
constexpr std::uint64_t process_a_marker = 0xA11AULL;
constexpr std::uint64_t process_b_marker = 0xB22BULL;
constexpr os::kernel::ThreadId process_a_thread = 1U;
constexpr os::kernel::ThreadId process_b_thread = 2U;
constexpr os::kernel::Priority process_priority = 4U;

// The kernel's own namespace for the one interrupt source this boot image
// currently routes through Kernel::dispatch_interrupt() rather than
// PreemptionCoordinator - the discovered virtual-timer PPI, reused as a
// stand-in device source per docs/M7_9_USER_SPACE_DRIVERS.md. Source numbers
// are the kernel's own assignment, not something a device tree provides
// (interrupt.hpp), so this is a constant here rather than a discovery result,
// the same way process_a_thread and process_b_thread are.
constexpr os::kernel::InterruptSource boot_device_interrupt_source = 1U;

// Process A's program (install_process_a_program) reuses words 12-13 and
// 14-15 for the M7.9 proof, appended after the words the M7.5f-M7.6a proof
// already occupies (0-11). Both entry points are reached the same way word
// 8 (the send redirect) already is: the kernel rewrites elr_el1 in a saved
// frame rather than the program branching there itself, and places every
// register the entry point's bare svc needs - x0, x8 - in that same frame
// before the redirect, so neither entry point needs anything baked into its
// own bytes beyond the svc itself.
constexpr std::uint64_t interrupt_attach_entry_virtual =
    user_code_virtual + 12U * sizeof(std::uint32_t);
constexpr std::uint64_t interrupt_complete_entry_virtual =
    user_code_virtual + 14U * sizeof(std::uint32_t);

// M7.11's EL0 proof reuses the same redirect technique for the two
// address-space calls: a bare svc and a branch-to-self, with x0/x1/x8 placed
// by complete_after_switch. A never learns it is being redirected.
constexpr std::uint64_t address_space_create_entry_virtual =
    user_code_virtual + 16U * sizeof(std::uint32_t);
constexpr std::uint64_t address_space_destroy_entry_virtual =
    user_code_virtual + 18U * sizeof(std::uint32_t);
constexpr std::uint64_t fault_probe_entry_virtual =
    user_code_virtual + 20U * sizeof(std::uint32_t);

// The address A is sent to touch, and the region declared over it. Inside A's
// address space and deliberately never mapped, so the load faults; inside a
// declared region, so the kernel has a defined answer rather than a panic.
constexpr std::uint64_t fault_probe_virtual = 0x0000'0000'3000'0000ULL;

// Where A continues once its fault is answered, and where B is sent to answer
// it. Word indices are per-program - A and B have their own code pages - so
// these do not collide despite the arithmetic looking as though they might.
constexpr std::uint64_t fault_resume_entry_virtual =
    user_code_virtual + 21U * sizeof(std::uint32_t);
constexpr std::uint64_t pager_supply_entry_virtual =
    user_code_virtual + 16U * sizeof(std::uint32_t);

// How many address spaces this image can hold at once for spaces created from
// EL0. Two rather than one so the structure is a pool with a real bound and a
// real exhaustion answer rather than a single slot pretending to be one; two
// rather than many because nothing here needs more and every slot is a
// MachineAddressSpace plus a builder sitting in .bss whether used or not.
//
// Exhaustion returns an error to the caller. That is the no-allocator decision
// showing up where it is supposed to: running out of slots is a condition the
// caller is told about and can act on, never a kernel failure it cannot.
constexpr std::size_t max_el0_address_spaces = 2U;

// How long after a successful attach the stand-in device source is armed to
// assert. Short on purpose: nothing about this proof depends on real elapsed
// time the way M7.5i's contention test does (see its own comment), so there
// is no reason to spend more of the 12-second QEMU budget than it takes to
// prove the interrupt was delivered asynchronously rather than synchronously
// with the attach that armed it.
constexpr std::uint64_t stand_in_device_deadline_nanoseconds = 200'000ULL;

volatile std::uint32_t* boot_uart = nullptr;
std::uint64_t boot_start_now = 0U;
std::uint32_t el0_yield_count = 0U;
std::uint32_t timer_irq_count = 0U;
bool device_driver_attach_started = false;
os::kernel::CapabilityId boot_client_ipc_cap = os::kernel::invalid_capability;
os::kernel::CapabilityId boot_server_ipc_cap = os::kernel::invalid_capability;
os::kernel::CapabilityId boot_device_interrupt_cap = os::kernel::invalid_capability;
os::kernel::aarch64::GicV3PrimaryCpu boot_gic{};

// The space M7.11's boot proof creates and destroys after boot. Static because
// the kernel has no allocator to hold it in - which is the same reason the
// proof exists: an address space created after boot is the first thing in
// Cookie that is not a boot artifact.
os::kernel::aarch64::EarlyPageArena dynamic_arena{};
os::kernel::aarch64::EarlyStage1Builder dynamic_builder{dynamic_arena};
os::kernel::MachineAddressSpace dynamic_space{};

// The pool spaces created from EL0 live in. A real system needs this in the
// machine layer with the rest of address-space lifetime; it is here because
// this image is the only caller and putting it in `machine` before a second
// caller exists would be designing a general mechanism from one example.
struct El0AddressSpaceSlot final {
    os::kernel::aarch64::EarlyPageArena arena{};
    os::kernel::aarch64::EarlyStage1Builder builder{};
    os::kernel::MachineAddressSpace space{};
    os::kernel::AddressSpaceIdentity identity{};
    bool occupied{false};
};
El0AddressSpaceSlot el0_address_spaces[max_el0_address_spaces]{};

// The page A names as the root of the space it creates. Boot selects it and
// hands it over in x1, because a process cannot yet name memory it owns -
// memory capabilities are what will eventually authorize this, and the
// creation-authority object is standing in for them meanwhile.
os::kernel::MemoryGrantAuthority boot_grants{};
// The capability A presents as its claim on the page its space is built from.
// Not the page number: a process names authority, never a physical address.
os::kernel::CapabilityId el0_space_root_grant = os::kernel::invalid_capability;
os::kernel::CapabilityId el0_space_authority = os::kernel::invalid_capability;
os::kernel::CapabilityId el0_space_capability = os::kernel::invalid_capability;
// 0 nothing yet, 1 created, 2 destroyed. Drives the redirect chain the same
// way el0_yield_count drives the M7.5f/M7.6a one.
std::uint32_t el0_space_stage = 0U;
// How many times A has successfully created a space. The second one is the
// interesting one: it proves the memory capability survived the first space's
// destruction, which is the loan reading of donation.
std::uint32_t el0_space_creates = 0U;
// Per-address-space by design (see fault_region.hpp); this harness runs one,
// for process A.
os::kernel::FaultRegionTable boot_fault_regions{};
os::kernel::FaultDeliveryTable boot_fault_deliveries{};
bool fault_probe_armed = false;
// B answers A's faults. One pager for one faulting thread, named at boot
// because nothing yet registers a pager - that is a capability-gated call this
// harness does not need to invent to prove the handshake works.
constexpr os::kernel::ThreadId boot_pager_thread = process_b_thread;
os::kernel::CapabilityId boot_pager_backing_cap = os::kernel::invalid_capability;
std::uint64_t boot_pager_backing_page = 0ULL;
bool pager_question_delivered = false;
bool fault_backed = false;
bool fault_resume_reported = false;
bool device_proof_complete = false;

os::kernel::MachinePhysicalLedger boot_physical_ledger{};
os::kernel::MachineAddressSpace boot_kernel_space{};
os::kernel::MachineAddressSpace process_space_a{};
os::kernel::MachineAddressSpace process_space_b{};
os::kernel::AddressSpaceEpochAuthority boot_epochs{};
os::kernel::ProcessTranslationTable boot_translations{};
os::kernel::Kernel boot_kernel{};
os::kernel::aarch64::PreemptionCoordinator boot_preemption{};

// Storage, ledger and address-space state for the early identity map.
//
// The ledger is its own rather than boot_physical_ledger, because the two
// maps genuinely do disagree about permissions on the same physical memory:
// a_code and b_code are mapped writable here so the boot routine can install
// each process's program into them, and read-execute in the real map so the
// process can run it. Sharing a ledger would make that pair a
// writable_executable_alias and refuse it - correctly, on the information a
// ledger has. The two maps are sequential, not concurrent, and the early one
// is abandoned before any of that memory becomes executable, which is a fact
// about boot order that no cross-space check can see. Separating the ledgers
// states the boundary instead of weakening the check.
alignas(4096) std::byte early_identity_table_storage[
    early_identity_table_pages * static_cast<std::size_t>(page_size)] {};
os::kernel::MachinePhysicalLedger early_identity_ledger{};
os::kernel::MachineAddressSpace early_identity_space{};

[[noreturn]] void halt() noexcept {
    asm volatile("msr daifset, #0xf" ::: "memory");
    for (;;) asm volatile("wfe" ::: "memory");
}

[[nodiscard]] constexpr std::uint64_t align_down(std::uint64_t value) noexcept {
    return value & ~(page_size - 1ULL);
}

[[nodiscard]] bool align_up(std::uint64_t value, std::uint64_t& out) noexcept {
    if (value > UINT64_MAX - (page_size - 1ULL)) return false;
    out = (value + page_size - 1ULL) & ~(page_size - 1ULL);
    return true;
}

[[nodiscard]] std::uint32_t read_be32(const std::byte* p) noexcept {
    return
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(p[0])) << 24U) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(p[1])) << 16U) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(p[2])) << 8U) |
        static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(p[3]));
}

[[nodiscard]] os::core::ByteSpan bounded_dtb(std::uintptr_t physical) noexcept {
    if (physical == 0U) return {};
    const auto* header = reinterpret_cast<const std::byte*>(physical);
    if (read_be32(header) != os::kernel::FdtView::magic) return {};
    const std::uint32_t total = read_be32(header + 4U);
    if (total < os::kernel::FdtView::header_bytes || total > max_boot_dtb_bytes) return {};
    return os::core::ByteSpan{header, static_cast<std::size_t>(total)};
}

void uart_write_char(char value) noexcept {
    if (boot_uart == nullptr) return;
    constexpr std::size_t fr_index = 0x18U / sizeof(std::uint32_t);
    constexpr std::uint32_t tx_fifo_full = 1U << 5U;
    while ((boot_uart[fr_index] & tx_fifo_full) != 0U) asm volatile("yield" ::: "memory");
    boot_uart[0] = static_cast<std::uint32_t>(static_cast<unsigned char>(value));
}

void uart_write(std::string_view text) noexcept {
    for (char c : text) uart_write_char(c);
}

// Fixed 16 digits, no "0x" and no leading-zero suppression. A fault report is
// read by grep and by eye under time pressure, and a fixed width means two
// addresses can be compared by looking at them.
void uart_write_hex(std::uint64_t value) noexcept {
    for (int shift = 60; shift >= 0; shift -= 4) {
        const auto digit = static_cast<unsigned>((value >> shift) & 0xFULL);
        uart_write_char(static_cast<char>(digit < 10U ? '0' + digit : 'a' + (digit - 10U)));
    }
}

// Early boot has roughly forty ways to stop and, until this existed, one
// observable outcome for all of them: a silent WFE loop that CI could only
// report as a timeout. The stage name costs a string literal and turns "the
// kernel hung" into "the kernel rejected the memory plan".
[[noreturn]] void fail(std::string_view stage) noexcept {
    uart_write("COOKIE:PANIC:");
    uart_write(stage);
    uart_write("\n");
    halt();
}

// The checks that run before the console is discovered cannot print a stage,
// so the QEMU trace address is the only evidence. One shared halt() destroys
// that evidence: every early failure folds onto a single address and
// addr2line attributes them all to whichever call site the compiler outlined
// first, which is how a failing parse came to look like a failing DTB probe.
//
// Each gets its own address. The distinct register write is not diagnostic in
// itself - nothing reads x20 - it exists so the bodies are not identical, and
// therefore cannot be folded back together by the linker.
[[gnu::noinline]] [[noreturn]] void halt_no_dtb() noexcept {
    asm volatile("mov x20, #1" ::: "x20");
    halt();
}
[[gnu::noinline]] [[noreturn]] void halt_unparsable_fdt() noexcept {
    asm volatile("mov x20, #2" ::: "x20");
    halt();
}
[[gnu::noinline]] [[noreturn]] void halt_no_inventory() noexcept {
    asm volatile("mov x20, #3" ::: "x20");
    halt();
}
[[gnu::noinline]] [[noreturn]] void halt_no_console() noexcept {
    asm volatile("mov x20, #4" ::: "x20");
    halt();
}
[[gnu::noinline]] [[noreturn]] void halt_no_vectors() noexcept {
    asm volatile("mov x20, #5" ::: "x20");
    halt();
}
// Covers every step of building the early identity map short of activating
// it - arena/builder setup, binding, manifest construction, replay. All of
// it operates on link-time-known symbols and an already-validated DTB range,
// so failure here means a real defect rather than a data-dependent case worth
// distinguishing further, the same reasoning halt_no_vectors already rests on.
[[gnu::noinline]] [[noreturn]] void halt_early_identity_map() noexcept {
    asm volatile("mov x20, #6" ::: "x20");
    halt();
}
// The early map's own activate_stage1_translation call, split from
// halt_early_identity_map because it is the highest-risk step - the first
// point translation turns on for this image - and deserves its own address
// the same way the real map's later activation gets its own fail("ACTIVATE_MMU").
[[gnu::noinline]] [[noreturn]] void halt_early_mmu() noexcept {
    asm volatile("mov x20, #7" ::: "x20");
    halt();
}

// discover_hardware has five distinct ways to refuse and they all arrive as
// one Result. Collapsed onto a single halt they were indistinguishable, so
// the error code has to become part of the address.
[[gnu::noinline]] [[noreturn]] void halt_inv_cells() noexcept {
    asm volatile("mov x20, #70" ::: "x20");
    halt();
}
[[gnu::noinline]] [[noreturn]] void halt_inv_reg() noexcept {
    asm volatile("mov x20, #71" ::: "x20");
    halt();
}
[[gnu::noinline]] [[noreturn]] void halt_inv_depth() noexcept {
    asm volatile("mov x20, #72" ::: "x20");
    halt();
}
[[gnu::noinline]] [[noreturn]] void halt_inv_exhausted() noexcept {
    asm volatile("mov x20, #73" ::: "x20");
    halt();
}
[[gnu::noinline]] [[noreturn]] void halt_inv_property() noexcept {
    asm volatile("mov x20, #74" ::: "x20");
    halt();
}

[[noreturn]] void halt_inventory_error(os::core::Error error) noexcept {
    switch (error.code) {
    case os::kernel::hardware_inventory_errors::unsupported_cells: halt_inv_cells();
    case os::kernel::hardware_inventory_errors::malformed_reg: halt_inv_reg();
    case os::kernel::hardware_inventory_errors::depth_exhausted: halt_inv_depth();
    case os::kernel::hardware_inventory_errors::inventory_exhausted: halt_inv_exhausted();
    case os::kernel::hardware_inventory_errors::invalid_property: halt_inv_property();
    default: halt_no_inventory();
    }
}

[[nodiscard]] const os::kernel::DiscoveredDevice*
find_pl011(const os::kernel::HardwareInventory& inventory) noexcept {
    for (std::size_t i = 0U; i < inventory.device_count; ++i) {
        if (inventory.devices[i].compatible_view() == "arm,pl011") return &inventory.devices[i];
    }
    return nullptr;
}

[[nodiscard]] bool add_manifest_range(
    os::kernel::aarch64::KernelMappingManifest& manifest,
    std::uint64_t virtual_begin,
    std::uint64_t physical_begin,
    std::uint64_t length,
    MachinePermissions permissions,
    MachineMemoryKind kind,
    os::kernel::aarch64::KernelMappingRole role =
        os::kernel::aarch64::KernelMappingRole::ordinary) noexcept {
    if (length == 0ULL || length > static_cast<std::uint64_t>(SIZE_MAX)) return false;
    return static_cast<bool>(manifest.add({
        .virtual_base = virtual_begin,
        .physical_base = physical_begin,
        .length = length,
        .permissions = permissions,
        .kind = kind,
        .role = role,
    }));
}

[[nodiscard]] bool add_identity_symbols(
    os::kernel::aarch64::KernelMappingManifest& manifest,
    const char* begin,
    const char* end,
    MachinePermissions permissions) noexcept {
    const auto start = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(begin));
    const auto finish = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(end));
    if (finish < start || (start & (page_size - 1ULL)) != 0ULL ||
        (finish & (page_size - 1ULL)) != 0ULL) return false;
    if (finish == start) return true;
    return add_manifest_range(
        manifest, start, start, finish - start,
        permissions, MachineMemoryKind::normal);
}

[[nodiscard]] bool add_device_manifest(
    os::kernel::aarch64::KernelMappingManifest& manifest,
    HardwareRange range) noexcept {
    if (!range.valid()) return false;
    const auto begin = align_down(range.base);
    std::uint64_t end = 0ULL;
    if (range.base > UINT64_MAX - range.size ||
        !align_up(range.base + range.size, end) || end <= begin) return false;
    return add_manifest_range(
        manifest, begin, begin, end - begin,
        MachinePermissions::read_write, MachineMemoryKind::device);
}

// Adds one range to the early identity map while that map is already the
// live address space. Everything boot touches physically between the two
// activations has to arrive this way: the map built before the device tree
// was parsed could only cover what was known then - the kernel image and the
// DTB - and every region chosen out of discovered RAM afterwards is outside
// it. Missing one does not corrupt anything; it takes a Data Abort, which
// with the console itself unmapped means the fault handler's own uart_write
// faults again and the machine loops in the vector reporting nothing.
[[nodiscard]] bool extend_early_identity_map(
    HardwareRange range,
    MachinePermissions permissions,
    MachineMemoryKind kind) noexcept {
    if (!range.valid()) return false;
    const auto begin = align_down(range.base);
    std::uint64_t end = 0ULL;
    if (range.base > UINT64_MAX - range.size ||
        !align_up(range.base + range.size, end) || end <= begin) return false;
    if (!os::kernel::machine_map(
            early_identity_space,
            static_cast<std::uintptr_t>(begin),
            static_cast<std::uintptr_t>(begin),
            static_cast<std::size_t>(end - begin),
            permissions, kind)) return false;
    // The descriptors were written as ordinary stores. The table walker is a
    // separate observer, so they have to be published before the access that
    // depends on them. No TLB invalidation: each entry moved from invalid to
    // valid, so there is no stale translation to remove.
    asm volatile("dsb ish" ::: "memory");
    asm volatile("isb" ::: "memory");
    return true;
}

// True when any word across `pages` pages from `base` is non-zero.
//
// Reads through volatile for the same reason the zeroing writes through it:
// this is called twice over the same range with a destroy in between, and
// nothing the compiler can see writes to that range in this translation unit -
// the writes happen inside aarch64_release_address_space. Ordinary loads would
// let it fold the second call into the first and answer from a cached value,
// which would make the proof pass whether reclamation ran or not.
[[nodiscard]] bool any_nonzero_page(std::uint64_t base, std::size_t pages) noexcept {
    const auto* words = reinterpret_cast<const volatile std::uint64_t*>(
        static_cast<std::uintptr_t>(base));
    const std::size_t count =
        pages * static_cast<std::size_t>(page_size) / sizeof(std::uint64_t);
    for (std::size_t index = 0U; index < count; ++index) {
        if (words[index] != 0ULL) return true;
    }
    return false;
}

void zero_page(std::uint64_t physical_page) noexcept {
    auto* words = reinterpret_cast<volatile std::uint64_t*>(
        static_cast<std::uintptr_t>(physical_page));
    for (std::size_t i = 0U; i < page_size / sizeof(std::uint64_t); ++i) words[i] = 0ULL;
}

void write_physical_bytes(
    std::uint64_t physical_page,
    std::size_t offset,
    std::span<const std::byte> bytes) noexcept {
    auto* destination = reinterpret_cast<volatile std::byte*>(
        static_cast<std::uintptr_t>(physical_page + offset));
    for (std::size_t i = 0U; i < bytes.size(); ++i) destination[i] = bytes[i];
}

void install_process_a_program(std::uint64_t physical_page) noexcept {
    zero_page(physical_page);
    auto* words = reinterpret_cast<volatile std::uint32_t*>(
        static_cast<std::uintptr_t>(physical_page));
    constexpr std::uint32_t movz_x8_base = 0xD2800008U;
    constexpr std::uint32_t movz_x19_base = 0xD2800013U;
    constexpr std::uint32_t svc_zero = 0xD4000001U;
    constexpr std::uint32_t branch_self = 0x14000000U;
    constexpr std::uint32_t yield_number =
        static_cast<std::uint32_t>(os::kernel::KernelCall::yield);
    words[0] = movz_x8_base | (yield_number << 5U);
    words[1] = svc_zero;
    words[2] = movz_x8_base | (yield_number << 5U);
    words[3] = svc_zero;
    words[4] = movz_x19_base | (static_cast<std::uint32_t>(process_a_marker) << 5U);
    words[5] = movz_x8_base | (yield_number << 5U);
    words[6] = svc_zero;
    words[7] = branch_self;

    // Timer milestone redirects A here with send arguments already in x0..x2.
    words[8] = svc_zero;
    words[9] = movz_x8_base | (yield_number << 5U);
    words[10] = svc_zero;
    words[11] = branch_self;

    // M7.9 redirects A here once, after the M7.6a proof concludes, with the
    // interrupt capability in x0 and interrupt_attach's call number in x8
    // already set by complete_after_switch - see interrupt_attach_entry_virtual.
    // Bare svc/branch_self, the same shape word 8 already uses for the send
    // redirect: A never needs to know the capability's value, the call
    // number, or that it is being redirected at all.
    words[12] = svc_zero;
    words[13] = branch_self;

    // M7.9 redirects A here whenever complete_interrupt_current delivers a
    // service - see interrupt_complete_entry_virtual - with the same
    // capability and interrupt_complete's call number placed in x0/x8 by
    // complete_after_switch.
    words[14] = svc_zero;
    words[15] = branch_self;

    // M7.11 redirects A here once the device proof concludes, to create an
    // address space and then to destroy the one it created - see
    // address_space_create_entry_virtual. Same bare svc and branch-to-self as
    // every redirect above: A never needs the capability's value, the call
    // number, or to know it is being redirected.
    words[16] = svc_zero;
    words[17] = branch_self;
    words[18] = svc_zero;
    words[19] = branch_self;

    // M7.11's fault-disclosure proof. `ldr x0, [x1]` against an address the
    // redirect places in x1 - a page inside a declared region that has no
    // backing - so the load takes a translation fault the kernel has a defined
    // answer for. A load rather than a store because a read is the weaker case:
    // if the kernel discloses on a read it discloses on a write too.
    constexpr std::uint32_t ldr_x0_x1 = 0xF9400020U;
    words[20] = ldr_x0_x1;
    // Where A lands if the load succeeds - which it only does if a pager
    // supplied backing and the kernel resumed it. A yield rather than a
    // branch-to-self, because "A continued" has to be observable: a thread
    // spinning silently is indistinguishable from one that never resumed.
    words[21] = movz_x8_base | (yield_number << 5U);
    words[22] = svc_zero;
    words[23] = branch_self;
}

void install_process_b_program(std::uint64_t physical_page) noexcept {
    zero_page(physical_page);
    auto* words = reinterpret_cast<volatile std::uint32_t*>(
        static_cast<std::uintptr_t>(physical_page));
    constexpr std::uint32_t movz_x19_base = 0xD2800013U;
    constexpr std::uint32_t movz_x1_base = 0xD2800001U;
    constexpr std::uint32_t movk_x1_lsl16_base = 0xF2A00001U;
    constexpr std::uint32_t movz_x2_base = 0xD2800002U;
    constexpr std::uint32_t movz_x8_base = 0xD2800008U;
    constexpr std::uint32_t svc_zero = 0xD4000001U;
    constexpr std::uint32_t branch_self = 0x14000000U;
    constexpr std::uint32_t reply_number =
        static_cast<std::uint32_t>(os::kernel::KernelCall::reply);
    constexpr std::uint32_t response_low =
        static_cast<std::uint32_t>(ipc_server_response & 0xFFFFULL);
    constexpr std::uint32_t response_high =
        static_cast<std::uint32_t>((ipc_server_response >> 16U) & 0xFFFFULL);

    words[0] = movz_x19_base | (static_cast<std::uint32_t>(process_b_marker) << 5U);
    words[1] = branch_self;

    // Timer milestone redirects B here with receive args in x0/x1. On return x0
    // is the opaque transaction token; build reply args without exposing a seal.
    words[8] = svc_zero;
    words[9] = movz_x1_base | (response_low << 5U);
    words[10] = movk_x1_lsl16_base | (response_high << 5U);
    words[11] = movz_x2_base | (static_cast<std::uint32_t>(ipc_response_size) << 5U);
    words[12] = movz_x8_base | (reply_number << 5U);
    words[13] = svc_zero;
    words[14] = branch_self;

    // B's pager entry. The kernel redirects it here when A faults in a region
    // B answers for, with the backing capability in x0 and fault_supply's call
    // number in x8 - the same bare-svc shape every other redirect uses, so B
    // needs nothing baked into its own bytes beyond the svc.
    //
    // What B is *told* arrives in x2: the region. Not the address, which is
    // the entire point of the fault path - see docs/M7_11_FAULT_PRIVACY.md.
    // B never learns where in its client's address space the fault happened.
    words[16] = svc_zero;
    words[17] = branch_self;
}

// Arms the reused virtual-timer PPI (boot_gic.device_intid) as M7.9's
// stand-in device source. Not a general machine-layer primitive - the choice
// to reuse this PPI as a device at all is specific to this boot harness's
// proof (see arch_timer_discovery.hpp and docs/M7_9_USER_SPACE_DRIVERS.md,
// "Which device source the first proof uses"), so it stays local here rather
// than joining machine_set_timer, which arms the *physical* comparator this
// image's own preemption depends on and must not be confused with this one.
// Mirrors machine_set_timer's read-frequency/read-counter/write-deadline/
// enable sequence exactly, against cntv_cval_el0/cntv_ctl_el0 instead of
// cntp_cval_el0/cntp_ctl_el0.
void arm_stand_in_device_source(std::uint64_t nanoseconds) noexcept {
    std::uint64_t frequency_raw = 0ULL;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(frequency_raw));
    const auto frequency = frequency_raw & 0xFFFF'FFFFULL;

    std::uint64_t now = 0ULL;
    asm volatile("mrs %0, cntvct_el0" : "=r"(now));

    // No overflow guard needed the way machine_set_timer's does: nanoseconds
    // here is the fixed, small stand_in_device_deadline_nanoseconds, not a
    // caller-supplied budget, so this product is bounded well under 2^64 for
    // any frequency an architected timer actually reports.
    const std::uint64_t ticks = (nanoseconds * frequency) / 1'000'000'000ULL;
    const std::uint64_t deadline = now + ticks;

    asm volatile("msr cntv_cval_el0, %0" :: "r"(deadline) : "memory");
    asm volatile("isb" ::: "memory");
    const std::uint64_t enable = 1ULL;
    asm volatile("msr cntv_ctl_el0, %0" :: "r"(enable) : "memory");
    asm volatile("isb" ::: "memory");
}

// Disables the comparator arm_stand_in_device_source armed. A level-triggered
// line stays asserted for as long as its condition holds, and a comparator's
// condition ("counter has passed the deadline") never clears itself the way a
// real device's would once serviced - CNTV_CTL_EL0.ISTATUS stays set forever
// after the deadline passes, so leaving ENABLE set would re-assert the line
// the instant gic_v3_set_ppi_masked unmasks it, indefinitely. Real hardware
// does not need this: this function exists only because a comparator is
// standing in for one.
void disarm_stand_in_device_source() noexcept {
    const std::uint64_t disable = 0ULL;
    asm volatile("msr cntv_ctl_el0, %0" :: "r"(disable) : "memory");
    asm volatile("isb" ::: "memory");
}

[[nodiscard]] bool commit_result(
    const os::kernel::aarch64::PreemptionResult& result,
    std::uint64_t now_nanoseconds) noexcept {
    auto plan = os::kernel::aarch64::prepare_execution_universe(result);
    if (!plan) return false;
    return static_cast<bool>(
        os::kernel::aarch64::commit_execution_universe(plan.value(), now_nanoseconds));
}

[[nodiscard]] bool verify_user_bytes(
    os::kernel::ThreadId thread,
    std::uint64_t address,
    std::span<const std::byte> expected) noexcept {
    auto binding = boot_translations.resolve(thread, boot_epochs);
    if (!binding || expected.empty()) return false;
    auto ticket = os::kernel::prepare_user_access(
        thread,
        binding.value().epoch,
        os::kernel::UserRange{address, expected.size()},
        os::kernel::UserAccessIntent::read_from_user,
        boot_translations,
        boot_epochs);
    if (!ticket) return false;
    std::array<std::byte, os::kernel::max_ipc_inline_bytes> actual{};
    auto copied = os::kernel::aarch64::copy_from_user_current(
        ticket.value(),
        std::span<std::byte>{actual.data(), expected.size()},
        boot_translations,
        boot_epochs);
    if (!copied) return false;
    for (std::size_t i = 0U; i < expected.size(); ++i) {
        if (actual[i] != expected[i]) return false;
    }
    return true;
}

[[nodiscard]] bool complete_after_switch(
    os::kernel::ThreadId next,
    os::kernel::aarch64::ExceptionFrame& frame) noexcept {
    auto completed = os::kernel::aarch64::complete_ipc_current(
        next, frame, boot_kernel, boot_translations, boot_epochs);
    if (!completed) return false;
    // Interrupt delivery uses x2/x3, IPC completion x0/x1 (see
    // aarch64_interrupt_syscall.hpp) - both run on every resume and neither
    // depends on the other having found anything.
    auto serviced = os::kernel::aarch64::complete_interrupt_current(
        next, frame, boot_kernel);
    if (!serviced) return false;

    if (serviced.value()) {
        // A device interrupt was just delivered to A's resume. Hand it
        // straight to the code that calls interrupt_complete, the same way
        // the M7.6a timer redirect already hands A straight to the code that
        // calls send rather than requiring A's own program to poll for it -
        // see interrupt_complete_entry_virtual. x2/x3 already carry the
        // delivery itself (complete_interrupt_current, above); x0/x8 are
        // interrupt_complete's own arguments, unrelated to the delivery, and
        // still have to be placed here because interrupt_complete_entry's
        // bare svc does not set them itself.
        frame.elr_el1 = interrupt_complete_entry_virtual;
        frame.x[0] = boot_device_interrupt_cap;
        frame.x[8] = static_cast<std::uint64_t>(os::kernel::KernelCall::interrupt_complete);
        uart_write("COOKIE:M7.9:SERVICED\n");
    } else if (next == process_a_thread && el0_yield_count == 4U &&
               !device_driver_attach_started) {
        // The M7.6a proof concluded and this is A's first resume since. Send
        // it to attach a real interrupt source exactly once - see
        // interrupt_attach_entry_virtual and boot_device_interrupt_cap,
        // minted alongside boot_client_ipc_cap/boot_server_ipc_cap.
        device_driver_attach_started = true;
        frame.elr_el1 = interrupt_attach_entry_virtual;
        frame.x[0] = boot_device_interrupt_cap;
        frame.x[8] = static_cast<std::uint64_t>(os::kernel::KernelCall::interrupt_attach);
    } else if (next == process_a_thread && device_proof_complete && el0_space_stage == 0U) {
        // The device proof concluded. Send A to create an address space -
        // the first time in Cookie that a *process* rather than the boot
        // routine asks for one.
        el0_space_stage = 1U;
        frame.elr_el1 = address_space_create_entry_virtual;
        frame.x[0] = el0_space_authority;
        frame.x[1] = el0_space_root_grant;
        frame.x[8] = static_cast<std::uint64_t>(os::kernel::KernelCall::address_space_create);
    } else if (next == process_a_thread && el0_space_stage == 1U &&
               el0_space_capability != os::kernel::invalid_capability) {
        // A's create returned, and x0 carried the capability back to it. The
        // destroy redirect uses the kernel's own record of that capability
        // rather than reading it out of A's registers: what A holds is what
        // this proof is testing, not what it is driven by.
        el0_space_stage = 2U;
        frame.elr_el1 = address_space_destroy_entry_virtual;
        frame.x[0] = el0_space_capability;
        frame.x[8] = static_cast<std::uint64_t>(os::kernel::KernelCall::address_space_destroy);
    } else if (next == process_a_thread && el0_space_stage == 2U) {
        // Create again, with the *same* memory capability A used the first
        // time. This is what turns donation-as-a-loan from something the code
        // happens to do into something it is gated on.
        //
        // Donating a page does not consume the grant. The alternative -
        // revoking it on donate and re-issuing one on destroy - was rejected
        // because it would make the kernel an origin of grants, and boot being
        // the only origin is what stops the grant table becoming a pool the
        // kernel dispenses from. Under the loan reading the holder's authority
        // never left, so there is nothing to hand back: what ends at destroy is
        // the space's use of the page, not A's claim on it.
        //
        // If that reading is wrong the second create fails, rather than the
        // proof passing because nothing ever tried.
        el0_space_stage = 3U;
        frame.elr_el1 = address_space_create_entry_virtual;
        frame.x[0] = el0_space_authority;
        frame.x[1] = el0_space_root_grant;
        frame.x[8] = static_cast<std::uint64_t>(os::kernel::KernelCall::address_space_create);
    } else if (next == process_a_thread && el0_space_stage == 3U &&
               el0_space_capability != os::kernel::invalid_capability) {
        // And tear the second one down too, so the proof ends with nothing
        // live rather than a space left over that nobody destroyed.
        el0_space_stage = 4U;
        frame.elr_el1 = address_space_destroy_entry_virtual;
        frame.x[0] = el0_space_capability;
        frame.x[8] = static_cast<std::uint64_t>(os::kernel::KernelCall::address_space_destroy);
    } else if (next == boot_pager_thread && boot_fault_deliveries.armed(next) &&
               !pager_question_delivered) {
        // The pager is about to run and owes an answer it has not been told
        // about. Hand it the question here rather than making it call to ask -
        // the same "rides back on the wakeup" arrangement a driver's interrupt
        // service uses, and the reason there is no fault_receive call.
        auto question = boot_fault_deliveries.take(next);
        if (!question) {
            uart_write("COOKIE:PANIC:FAULT_TAKE\n");
            halt();
        }
        pager_question_delivered = true;
        frame.elr_el1 = pager_supply_entry_virtual;
        frame.x[0] = boot_pager_backing_cap;
        // The region, and only the region. x2 carries what the pager is
        // allowed to know; there is deliberately no register holding the
        // faulting address.
        frame.x[2] = static_cast<std::uint64_t>(question.value().region);
        frame.x[8] = static_cast<std::uint64_t>(os::kernel::KernelCall::fault_supply);
    } else if (next == process_a_thread && el0_space_stage == 4U && !fault_probe_armed) {
        // Last, because it ends the run: send A to touch an address inside a
        // declared region that has no backing. Everything above has already
        // reported by now, so a fault here cannot be mistaken for one of them
        // failing.
        fault_probe_armed = true;
        frame.elr_el1 = fault_probe_entry_virtual;
        frame.x[1] = fault_probe_virtual;
    }
    return true;
}

[[noreturn]] void guarded_runtime_main() noexcept {
    uart_write("COOKIE:M7.5d:GUARDED\n");

    const auto user_stack_top = user_stack_virtual + page_size;
    if (!os::kernel::aarch64_validate_user_context(
            process_space_a,
            static_cast<std::uintptr_t>(user_code_virtual),
            static_cast<std::uintptr_t>(user_stack_top)) ||
        !os::kernel::aarch64_validate_user_context(
            process_space_b,
            static_cast<std::uintptr_t>(user_code_virtual),
            static_cast<std::uintptr_t>(user_stack_top)) ||
        !boot_gic.initialized) {
        uart_write("COOKIE:PANIC:EL0_CONTEXT\n");
        halt();
    }

    os::kernel::aarch64::ExceptionFrame live{};
    const auto now = os::kernel::machine_monotonic_nanoseconds();
    boot_start_now = now;
    auto start = boot_preemption.start(
        boot_kernel.runqueue(), boot_translations, boot_epochs, now, live);
    if (!start || start.value().next != process_a_thread ||
        start.value().switched == false || start.value().deadline.active ||
        !commit_result(start.value(), now) ||
        !complete_after_switch(start.value().next, live)) {
        uart_write("COOKIE:PANIC:EL0_START\n");
        halt();
    }

    os::kernel::cookie_aarch64_enter_el0(live.elr_el1, live.sp_el0);
}

} // namespace

extern "C" [[noreturn]] void cookie_aarch64_unhandled_exception() noexcept {
    uart_write("COOKIE:PANIC:EXCEPTION\n");
    halt();
}

extern "C" [[noreturn]] void cookie_aarch64_unhandled_fault(
    const os::kernel::aarch64::ExceptionFrame* frame) noexcept {
    if (frame == nullptr) cookie_aarch64_unhandled_exception();
    using os::kernel::aarch64::AbortCause;
    using os::kernel::aarch64::FaultKind;
    const auto fault = os::kernel::aarch64::describe_fault(frame->esr_el1);

    uart_write("COOKIE:PANIC:FAULT:");
    switch (fault.kind) {
    case FaultKind::data_abort: uart_write("DATA_ABORT"); break;
    case FaultKind::instruction_abort: uart_write("INSN_ABORT"); break;
    case FaultKind::pc_alignment: uart_write("PC_ALIGN"); break;
    case FaultKind::sp_alignment: uart_write("SP_ALIGN"); break;
    case FaultKind::illegal_state: uart_write("ILLEGAL_STATE"); break;
    case FaultKind::simd_trap: uart_write("SIMD_TRAP"); break;
    case FaultKind::serror: uart_write("SERROR"); break;
    case FaultKind::unknown: uart_write("UNKNOWN"); break;
    }

    if (fault.kind == FaultKind::data_abort || fault.kind == FaultKind::instruction_abort) {
        uart_write(":");
        switch (fault.cause) {
        case AbortCause::address_size: uart_write("ADDR_SIZE"); break;
        case AbortCause::translation: uart_write("TRANSLATION"); break;
        case AbortCause::access_flag: uart_write("ACCESS_FLAG"); break;
        case AbortCause::permission: uart_write("PERMISSION"); break;
        case AbortCause::external: uart_write("EXTERNAL"); break;
        case AbortCause::alignment: uart_write("ALIGNMENT"); break;
        case AbortCause::tlb_conflict: uart_write("TLB_CONFLICT"); break;
        case AbortCause::unknown: uart_write("UNKNOWN"); break;
        }
        if (fault.level_meaningful()) {
            uart_write(":L");
            uart_write_char(static_cast<char>('0' + fault.level));
        }
        if (fault.kind == FaultKind::data_abort) {
            uart_write(fault.write ? ":WRITE" : ":READ");
        }
    }
    uart_write(fault.from_lower_el ? ":EL0" : ":EL1");

    // FAR only when the syndrome says it holds the faulting address. Printing
    // it unconditionally would put a stale register value next to a fault that
    // did not set it, which is worse than printing nothing.
    if (fault.far_valid) {
        uart_write(" far=");
        uart_write_hex(frame->far_el1);
    }
    uart_write(" elr=");
    uart_write_hex(frame->elr_el1);
    uart_write(" esr=");
    uart_write_hex(frame->esr_el1);
    uart_write("\n");
    halt();
}

namespace {

[[nodiscard]] El0AddressSpaceSlot* free_el0_address_space() noexcept {
    for (auto& slot : el0_address_spaces) if (!slot.occupied) return &slot;
    return nullptr;
}

[[nodiscard]] El0AddressSpaceSlot* el0_address_space_for(
    os::kernel::AddressSpaceIdentity identity) noexcept {
    if (!identity.valid()) return nullptr;
    for (auto& slot : el0_address_spaces) {
        if (slot.occupied && slot.identity == identity) return &slot;
    }
    return nullptr;
}

// The machine half and the kernel half of creating a space, in the order the
// circular dependency permits: bind a rootless builder, take the caller's page,
// build the root from it, and only then ask the kernel to authorize and name
// the result. Authorizing first would mint a capability for a space that might
// still fail to get a root, and the capability would then name nothing.
[[nodiscard]] os::core::Result<os::kernel::CapabilityId> create_el0_address_space(
    El0AddressSpaceSlot& slot,
    os::kernel::ThreadId creator,
    os::kernel::CapabilityId authority,
    os::kernel::CapabilityId root_grant) noexcept {
    // The caller's claim on the memory, checked before anything is built from
    // it. This is what stops a process naming a page nobody gave it: the
    // capability must be held, carry memory_right_donate, name a memory grant,
    // and that grant must still be live.
    auto granted = boot_kernel.memory_for_capability(
        creator, root_grant, os::kernel::memory_right_donate, boot_grants);
    if (!granted) return granted.error();
    // A grant may be larger than one page; the root takes the first. Refusing a
    // grant too small to hold one is the caller's error to see, not a donation
    // that half-works.
    if (!granted.value().contains(granted.value().physical_base, page_size)) {
        return os::core::make_error(
            os::core::ErrorDomain::kernel,
            os::kernel::memory_grant_errors::invalid_range);
    }
    const auto root_page = granted.value().physical_base;

    slot.arena = os::kernel::aarch64::EarlyPageArena{};
    // Constructed with the arena rather than attach_arena'd to it, and the
    // difference is not style. attach_arena validates the arena, and an arena
    // with no bump range and nothing donated yet is not valid - which is
    // precisely the state every post-boot arena starts in, because its pages
    // arrive by donation and donation needs the space already bound to this
    // builder. The same circular dependency that shaped create -> donate ->
    // initialize root, one level down.
    slot.builder = os::kernel::aarch64::EarlyStage1Builder{slot.arena};

    auto bound = os::kernel::aarch64_create_address_space(
        slot.space, boot_physical_ledger, slot.builder);
    if (!bound) return bound.error();

    auto donated = os::kernel::aarch64_donate_table_page(
        slot.space, slot.arena, static_cast<std::uintptr_t>(root_page));
    if (!donated) return donated.error();

    auto root = os::kernel::aarch64_initialize_translation_root(slot.space);
    if (!root) return root.error();

    auto created = boot_kernel.address_space_create(creator, authority, boot_epochs);
    if (!created) return created.error();

    slot.identity = created.value().epoch.identity();
    slot.occupied = true;
    return created.value().capability;
}

// Destroy in the order the ASID lifecycle requires: invalidate the software
// epoch, then let the machine layer tear down translations and invalidate
// TLBs, then release the ASID. The slot is only freed once all three have
// happened, so a failure part-way leaves it occupied rather than handing a
// half-retired space to the next caller.
[[nodiscard]] bool destroy_el0_address_space(
    os::kernel::ThreadId owner,
    os::kernel::CapabilityId capability) noexcept {
    auto retiring = boot_kernel.address_space_begin_destroy(
        owner, capability, boot_epochs);
    if (!retiring) return false;

    auto* slot = el0_address_space_for(retiring.value().epoch.identity());
    if (slot == nullptr) return false;

    if (!os::kernel::aarch64_release_address_space(slot->space, retiring.value())) {
        return false;
    }
    if (!boot_kernel.address_space_complete_destroy(
            owner, capability, retiring.value(), boot_epochs)) {
        return false;
    }
    slot->identity = os::kernel::AddressSpaceIdentity{};
    slot->occupied = false;
    return true;
}

} // namespace

// A synchronous EL0 fault that is not a system call.
//
// Reports what the kernel is willing to say, kills the thread that asked, and
// returns having chosen what runs next. The machine survives one process
// getting it wrong, which is the whole reason this is separate from the
// [[noreturn]] reporter above.
extern "C" void cookie_aarch64_el0_fault(
    os::kernel::aarch64::ExceptionFrame* frame) noexcept {
    if (frame == nullptr) cookie_aarch64_unhandled_exception();
    using os::kernel::aarch64::FaultKind;
    const auto fault = os::kernel::aarch64::describe_fault(frame->esr_el1);
    const auto faulting = boot_preemption.running();

    // What the kernel says. resolve() reports the region the address falls in,
    // never the address - see docs/M7_11_FAULT_PRIVACY.md. Consulted before
    // anything else prints, because a reporter that ran first would already
    // have disclosed FAR on the way here.
    if ((fault.kind == FaultKind::data_abort ||
         fault.kind == FaultKind::instruction_abort) && fault.far_valid) {
        const auto report = boot_fault_regions.resolve(frame->far_el1, fault.write);
        if (report.deliverable()) {
            uart_write("COOKIE:M7.11:FAULT_REGION region=");
            uart_write_hex(static_cast<std::uint64_t>(report.region));
            uart_write(fault.write ? " write=1" : " write=0");
            uart_write("\n");

            // Ask the pager rather than killing the thread. This is the
            // difference between a fault the system can answer and one it can
            // only report, and it is the only branch here that does not end
            // with the faulting thread dead.
            //
            // If arming fails - no pager, one already owing an answer, the
            // table full - the code falls through to the termination path
            // below rather than retrying. A fault that cannot be asked about
            // is unresolvable by definition, and the region's one announcement
            // is already spent either way, so there is nothing to preserve by
            // trying again.
            if (boot_fault_deliveries.arm(boot_pager_thread, faulting, report)) {
                // Blocked, not dead: A waits for backing rather than being
                // destroyed, which is what makes this a fault that resumes.
                if (!boot_kernel.runqueue().update(faulting, false, process_priority)) {
                    uart_write("COOKIE:PANIC:FAULT_UNRUNNABLE\n");
                    halt();
                }
                const auto asked_now = os::kernel::machine_monotonic_nanoseconds();
                auto asked = boot_preemption.reschedule(
                    boot_kernel.runqueue(), boot_translations, boot_epochs, asked_now, *frame,
                    boot_kernel.ipc_earliest_receive_deadline());
                if (!asked || !commit_result(asked.value(), asked_now) ||
                    !complete_after_switch(asked.value().next, *frame)) {
                    uart_write("COOKIE:PANIC:FAULT_ASK_RESCHEDULE\n");
                    halt();
                }
                uart_write("COOKIE:M7.11:FAULT_ASKED\n");
                return;
            }
        }
    }

    // No pager exists, so the question has nowhere to go and this fault is
    // unresolvable. That kills the thread, not the machine - the distinction
    // docs/M7_11_MEMORY.md's fault path is built on, and the reason halting
    // was only ever correct while nothing could possibly respond.
    // Three steps in an order that is not the obvious one, and the first
    // attempt got it wrong in a way worth recording.
    //
    // Destroying the thread first and then rescheduling fails: reschedule has
    // to save the outgoing thread's context, and destroy_thread has already
    // retired the bindings it needs to do that. It refuses, and what the log
    // then shows is a reschedule that would not run with a thread still alive.
    //
    // Simply swapping them is worse rather than better: the scheduler would
    // still see the faulting thread as runnable and could pick it again, and
    // the fault would repeat forever.
    //
    // So the runqueue is told first, which is the smallest true statement -
    // this thread is not runnable - and it is what lets the scheduler answer
    // the question honestly. Only once something else is running does the
    // thread get destroyed, by which point nothing needs it resolvable.
    if (!boot_kernel.runqueue().update(faulting, false, process_priority)) {
        uart_write("COOKIE:PANIC:FAULT_UNRUNNABLE\n");
        halt();
    }

    const auto now = os::kernel::machine_monotonic_nanoseconds();
    auto next = boot_preemption.reschedule(
        boot_kernel.runqueue(), boot_translations, boot_epochs, now, *frame,
        boot_kernel.ipc_earliest_receive_deadline());
    // Split rather than chained, and say which step and which thread. The
    // first version reported all three as one nothing, and "nothing runnable"
    // turned out to be the wrong name for it - the scheduler had a thread and
    // a later step refused.
    if (!next) {
        uart_write("COOKIE:PANIC:FAULT_RESCHEDULE live=");
        uart_write_hex(static_cast<std::uint64_t>(boot_kernel.live_thread_count()));
        uart_write("\n");
        halt();
    }
    uart_write("COOKIE:M7.11:FAULT_NEXT next=");
    uart_write_hex(static_cast<std::uint64_t>(next.value().next));
    uart_write(next.value().switched ? " switched=1" : " switched=0");
    uart_write(" live=");
    uart_write_hex(static_cast<std::uint64_t>(boot_kernel.live_thread_count()));
    uart_write("\n");
    if (!commit_result(next.value(), now) ||
        !complete_after_switch(next.value().next, *frame)) {
        // Nothing runnable is a real possibility rather than an invariant
        // violation - it is what happens when the last thread faults - so it
        // halts with a name instead of trapping.
        uart_write("COOKIE:PANIC:FAULT_COMMIT\n");
        halt();
    }

    // Something else is running now, so nothing needs the faulting thread
    // resolvable any more and it can go.
    auto teardown = boot_kernel.destroy_thread(faulting);
    if (!teardown) {
        uart_write("COOKIE:PANIC:FAULT_TEARDOWN\n");
        halt();
    }
    uart_write("COOKIE:M7.11:THREAD_TERMINATED\n");
    uart_write("COOKIE:M7.11:SURVIVED_FAULT\n");
}

extern "C" void cookie_kernel_syscall_entry(
    os::kernel::aarch64::ExceptionFrame* frame) noexcept {
    if (frame == nullptr) halt();
    auto call = os::kernel::decode_call(static_cast<std::uint16_t>(frame->x[8]));
    const auto current = boot_preemption.running();
    if (!call ||
        (call.value().authority != os::kernel::CallAuthority::unprivileged &&
         call.value().authority != os::kernel::CallAuthority::interrupt_control &&
         call.value().authority != os::kernel::CallAuthority::process_control &&
         call.value().authority != os::kernel::CallAuthority::memory_control) ||
        (current != process_a_thread && current != process_b_thread)) {
        uart_write("COOKIE:PANIC:EL0_SYSCALL\n");
        halt();
    }

    // process_control is admitted above for the two address-space calls, and
    // for nothing else yet. The other calls carrying that authority
    // (create_thread, destroy_thread, and the rest) have no dispatch here, so
    // an EL0 caller naming one would fall through every branch below and reach
    // the yield tail, which is a wrong answer rather than a refusal. Refused
    // explicitly instead: widening the authority check is not the same as
    // implementing what it lets in.
    if (call.value().authority == os::kernel::CallAuthority::process_control &&
        call.value().call != os::kernel::KernelCall::address_space_create &&
        call.value().call != os::kernel::KernelCall::address_space_destroy) {
        uart_write("COOKIE:PANIC:EL0_PROCESS_CONTROL\n");
        halt();
    }
    // Same rule for memory_control, and the same reason: map and unmap carry
    // that authority and have no dispatch here, so admitting the class without
    // refusing them would let a caller fall through to the yield tail and get a
    // wrong answer rather than a refusal.
    if (call.value().authority == os::kernel::CallAuthority::memory_control &&
        call.value().call != os::kernel::KernelCall::fault_supply) {
        uart_write("COOKIE:PANIC:EL0_MEMORY_CONTROL\n");
        halt();
    }

    if (call.value().call == os::kernel::KernelCall::fault_supply) {
        auto decoded = os::kernel::decode_fault_supply_syscall(frame->x[0]);
        if (!decoded) {
            uart_write("COOKIE:PANIC:FAULT_SUPPLY_DECODE\n");
            halt();
        }
        // The pager's claim on the memory it is offering. memory_right_map
        // rather than donate: backing is memory to be mapped for someone else,
        // not memory being spent on a kernel object.
        auto backing = boot_kernel.memory_for_capability(
            current, decoded.value().backing, os::kernel::memory_right_map, boot_grants);
        if (!backing) {
            frame->x[0] = 0ULL;
            uart_write("COOKIE:M7.11:SUPPLY_REFUSED\n");
            return;
        }
        // answer() is what proves this pager was actually asked. A pager that
        // was never asked, or has not collected its question, is refused here
        // rather than being allowed to push memory into somebody's address
        // space unprompted.
        auto answered = boot_fault_deliveries.answer(current);
        if (!answered) {
            frame->x[0] = 0ULL;
            uart_write("COOKIE:M7.11:SUPPLY_UNASKED\n");
            return;
        }

        // The backing goes in where the region is, which the kernel knows and
        // the pager was never told. This is the point at which withholding the
        // address costs nothing: the kernel had it all along.
        if (!os::kernel::aarch64_back_user_page(
                process_space_a,
                static_cast<std::uintptr_t>(fault_probe_virtual),
                static_cast<std::uintptr_t>(backing.value().physical_base),
                static_cast<std::size_t>(page_size),
                MachinePermissions::read_write)) {
            uart_write("COOKIE:PANIC:FAULT_BACKING_MAP\n");
            halt();
        }
        if (!boot_fault_regions.mark_backed(
                answered.value().region,
                os::kernel::FaultRegionState::backed_private)) {
            uart_write("COOKIE:PANIC:FAULT_MARK_BACKED\n");
            halt();
        }
        // The thread that faulted becomes runnable again. It resumes on the
        // instruction that faulted, which now succeeds - that is the whole
        // claim, and A's own yield afterwards is what makes it observable.
        if (!boot_kernel.runqueue().update(
                answered.value().faulting, true, process_priority)) {
            uart_write("COOKIE:PANIC:FAULT_RESUME_RUNNABLE\n");
            halt();
        }
        fault_backed = true;
        frame->x[0] = 1ULL;
        uart_write("COOKIE:M7.11:PAGER_BACKED\n");
        return;
    }

    if (call.value().call == os::kernel::KernelCall::address_space_create) {
        auto decoded = os::kernel::decode_address_space_create_syscall(
            frame->x[0], frame->x[1]);
        if (!decoded) {
            uart_write("COOKIE:PANIC:EL0_AS_DECODE\n");
            halt();
        }
        // Every failure below returns to A rather than halting, because each
        // one is a condition a caller could legitimately provoke - no free
        // slot, a page it does not own, an epoch table that is full. The
        // no-allocator decision is only worth anything if running out is an
        // answer the caller gets rather than a machine that stops.
        auto* slot = free_el0_address_space();
        if (slot == nullptr) {
            frame->x[0] = 0ULL;
            uart_write("COOKIE:M7.11:EL0_POOL_FULL\n");
            return;
        }
        auto created = create_el0_address_space(
            *slot, current, decoded.value().authority, decoded.value().root_grant);
        if (!created) {
            frame->x[0] = 0ULL;
            // With the code, because "refused" alone cost a CI round trip to
            // turn into "attach_arena rejected an arena that is empty by
            // construction". A refusal a caller can act on is also one a
            // reader has to be able to diagnose.
            uart_write("COOKIE:M7.11:EL0_CREATE_REFUSED code=");
            uart_write_hex(static_cast<std::uint64_t>(created.error().code));
            uart_write("\n");
            return;
        }
        el0_space_capability = created.value();
        frame->x[0] = created.value();
        ++el0_space_creates;
        uart_write("COOKIE:M7.11:EL0_CREATED\n");
        // Announced separately because it is a different claim. The first
        // create proves a process can make an address space; this one proves
        // the page it was built from came back when that space was destroyed,
        // and came back to the same holder rather than to a pool.
        if (el0_space_creates == 2U) uart_write("COOKIE:M7.11:EL0_GRANT_REUSED\n");
        return;
    }

    if (call.value().call == os::kernel::KernelCall::address_space_destroy) {
        auto decoded = os::kernel::decode_address_space_destroy_syscall(frame->x[0]);
        if (!decoded) {
            uart_write("COOKIE:PANIC:EL0_AS_DECODE\n");
            halt();
        }
        if (!destroy_el0_address_space(current, decoded.value().space)) {
            frame->x[0] = 0ULL;
            uart_write("COOKIE:M7.11:EL0_DESTROY_REFUSED\n");
            return;
        }
        frame->x[0] = 1ULL;
        uart_write("COOKIE:M7.11:EL0_DESTROYED\n");
        return;
    }

    if (call.value().call == os::kernel::KernelCall::send ||
        call.value().call == os::kernel::KernelCall::receive ||
        call.value().call == os::kernel::KernelCall::reply) {
        if (call.value().call == os::kernel::KernelCall::reply && current == process_b_thread) {
            const std::array<std::byte, ipc_request_size> expected{
                std::byte{'C'}, std::byte{'O'}, std::byte{'O'},
                std::byte{'K'}, std::byte{'I'}, std::byte{'E'}};
            if (!verify_user_bytes(
                    process_b_thread,
                    ipc_server_exchange,
                    std::span<const std::byte>{expected})) {
                uart_write("COOKIE:PANIC:IPC_REQUEST\n");
                halt();
            }
            uart_write("COOKIE:M7.6:IPC_REQUEST_B\n");
        }

        auto ipc = os::kernel::aarch64::dispatch_ipc_svc_current(
            current,
            call.value().call,
            *frame,
            boot_kernel,
            boot_translations,
            boot_epochs);
        if (!ipc) {
            uart_write("COOKIE:PANIC:IPC_SVC\n");
            halt();
        }
        if (!ipc.value().reschedule) return;

        const auto now = os::kernel::machine_monotonic_nanoseconds();
        // The reschedule that follows a blocking receive is what arms the
        // timer for it. Without the deadline here a bounded wait would only be
        // noticed at the next scheduling tick, and on an otherwise idle system
        // there is no next tick - the wait would never end.
        auto next = boot_preemption.reschedule(
            boot_kernel.runqueue(), boot_translations, boot_epochs, now, *frame,
            boot_kernel.ipc_earliest_receive_deadline());
        if (!next || !commit_result(next.value(), now) ||
            !complete_after_switch(next.value().next, *frame)) {
            uart_write("COOKIE:PANIC:IPC_SWITCH\n");
            halt();
        }
        return;
    }

    // Interrupt syscalls carry their one argument - the capability naming the
    // source - in x0, matching send/receive/reply's own convention of passing
    // a capability id in the first argument register. No caller in this boot
    // proof issues these yet (M7.9's end-to-end proof adds one); the decode is
    // wired ahead of that caller so the two land as independently reviewable
    // changes, the same order M7.5g's timer delivery and M7.5i's proof of it
    // landed in.
    if (call.value().call == os::kernel::KernelCall::interrupt_attach ||
        call.value().call == os::kernel::KernelCall::interrupt_detach ||
        call.value().call == os::kernel::KernelCall::interrupt_complete) {
        const auto source_capability =
            static_cast<os::kernel::CapabilityId>(frame->x[0]);

        // This boot image wires exactly one device source
        // (boot_device_interrupt_source, at boot_gic.device_intid) - the
        // capability check inside each call below already refused anything
        // that names a different object, so a call reaching this point can
        // only be about that one source. That is what makes it safe to name
        // boot_gic.device_intid directly here rather than threading an INTID
        // back out of the Kernel:: composition, which returns only what the
        // driver needs (attached/not, must-service-again) and deliberately
        // not the machine-layer detail of which controller line it maps to.
        // The day a second device source exists this stops being sound and
        // must change with it.
        if (call.value().call == os::kernel::KernelCall::interrupt_attach) {
            if (!boot_kernel.interrupt_attach(current, source_capability)) {
                uart_write("COOKIE:PANIC:INTERRUPT_ATTACH\n");
                halt();
            }
            // Configured but left disabled at boot (see
            // initialize_gic_v3_primary_cpu) so nothing can assert against a
            // source with no owner yet; enabled here, the instant one exists.
            os::kernel::aarch64::gic_v3_set_ppi_masked(
                boot_gic.redistributor, boot_gic.device_intid, false);
            uart_write("COOKIE:M7.9:ATTACHED\n");
            // Nothing external drives this stand-in source - it is the
            // discovered virtual-timer PPI, not a real peripheral - so the
            // proof arms it itself, immediately after the attach that would,
            // for a real device, be the point a driver becomes ready to
            // receive it. See arm_stand_in_device_source's own comment for
            // why this stays local to the boot harness.
            arm_stand_in_device_source(stand_in_device_deadline_nanoseconds);
            return;
        }
        if (call.value().call == os::kernel::KernelCall::interrupt_detach) {
            if (!boot_kernel.interrupt_detach(current, source_capability)) {
                uart_write("COOKIE:PANIC:INTERRUPT_DETACH\n");
                halt();
            }
            // InterruptTable's own contract: a detached source is left
            // masked. An owner-less source that can still assert is a
            // livelock nobody is positioned to stop.
            os::kernel::aarch64::gic_v3_set_ppi_masked(
                boot_gic.redistributor, boot_gic.device_intid, true);
            return;
        }

        // interrupt_complete's result carries whether InterruptTable::
        // end_service found the source still asserted and needing another
        // service pass - real information the driver needs, so it goes back
        // in x0 the same way a completed receive returns its byte count there.
        auto completed = boot_kernel.interrupt_complete(current, source_capability);
        if (!completed) {
            uart_write("COOKIE:PANIC:INTERRUPT_COMPLETE\n");
            halt();
        }
        // Unmask only when the answer is "nothing outstanding" (false):
        // end_service already re-armed InterruptTable's own state to pending
        // rather than attached when the device asserted again mid-service, and
        // unmasking the controller in that case would let a second interrupt
        // arrive for a source InterruptTable still considers masked - the
        // exact state mismatch the mask-until-complete rule exists to prevent.
        if (!completed.value()) {
            os::kernel::aarch64::gic_v3_set_ppi_masked(
                boot_gic.redistributor, boot_gic.device_intid, false);
        }
        frame->x[0] = completed.value() ? 1ULL : 0ULL;
        uart_write("COOKIE:M7.9:COMPLETED\n");
        // Releases M7.11's redirect chain. It waits for this rather than
        // running alongside, so a failure in either proof cannot be mistaken
        // for a failure in the other.
        device_proof_complete = true;
        return;
    }

    if (call.value().call != os::kernel::KernelCall::yield || current != process_a_thread) {
        uart_write("COOKIE:PANIC:EL0_SYSCALL\n");
        halt();
    }

    // A reached the instruction after its fault. That is the claim the whole
    // pager path exists to support, and it is only observable because A yields
    // here rather than spinning - a thread that resumed and a thread that never
    // did look identical from outside if neither makes a call.
    if (fault_backed && !fault_resume_reported) {
        fault_resume_reported = true;
        uart_write("COOKIE:M7.11:FAULT_RESUMED\n");
    }

    if (el0_yield_count == 0U) {
        frame->x[0] = syscall_return_cookie;
        el0_yield_count = 1U;
        return;
    }
    if (el0_yield_count == 1U && frame->x[0] == syscall_return_cookie) {
        el0_yield_count = 2U;
        uart_write("COOKIE:M7.5f:EL0_SVC_RETURN\n");
        return;
    }
    if (el0_yield_count == 2U && frame->x[19] == process_a_marker) {
        el0_yield_count = 3U;
        if (!boot_kernel.runqueue().update(process_b_thread, true, process_priority)) {
            uart_write("COOKIE:PANIC:SCHED_WAKE\n");
            halt();
        }

        // Deliberately boot_start_now, not a fresh machine_monotonic_nanoseconds()
        // read, as the basis for this decision.
        //
        // Scheduler::choose() charges all elapsed real time since the last
        // decision even while uncontested - the anti-gaming property that
        // stops a thread dodging its charge by avoiding decision points - and
        // that charging is correct and stays exactly as it is. The problem is
        // narrower: the kernel-internal cost of servicing two EL0/EL1 round
        // trips and a UART print is not the user thread's own work, and on
        // real hardware it is microseconds, well inside
        // default_slice_nanoseconds (2ms, a deliberate product constant - see
        // its own comment - not a value to loosen for a bring-up proof). Under
        // QEMU TCG on shared CI it was measured exceeding 2ms from the round
        // trips and print alone, exhausting process A's slice before this
        // deliberate contention test ever ran, on every attempt to shrink
        // that window including the round trip itself in isolation. Passing
        // the still-current since-start() timestamp keeps this decision
        // uncontested-in-effect regardless of how long the emulator actually
        // took, matching the real-hardware case this proof is meant to
        // establish. Genuine elapsed time returns for the on_timer() paths
        // below, which take their timestamp from the delivered interrupt.
        auto rescheduled = boot_preemption.reschedule(
            boot_kernel.runqueue(), boot_translations, boot_epochs, boot_start_now, *frame,
            boot_kernel.ipc_earliest_receive_deadline());
        if (!rescheduled || rescheduled.value().next != process_a_thread ||
            rescheduled.value().switched || !rescheduled.value().deadline.active ||
            !commit_result(rescheduled.value(), boot_start_now) ||
            !complete_after_switch(rescheduled.value().next, *frame)) {
            uart_write("COOKIE:PANIC:SCHED_EVENT\n");
            halt();
        }
        uart_write("COOKIE:M7.5i:CONTENTION_ARMED\n");
        return;
    }
    if (el0_yield_count == 3U && frame->x[19] == process_a_marker &&
        frame->x[0] == ipc_response_size) {
        const std::array<std::byte, ipc_response_size> expected{
            std::byte{'O'}, std::byte{'K'}};
        if (!verify_user_bytes(
                process_a_thread,
                ipc_client_exchange,
                std::span<const std::byte>{expected})) {
            uart_write("COOKIE:PANIC:IPC_REPLY\n");
            halt();
        }
        el0_yield_count = 4U;
        uart_write("COOKIE:M7.6:IPC_A_B_REPLY\n");
        return;
    }

    uart_write("COOKIE:PANIC:EL0_STATE\n");
    halt();
}

extern "C" void cookie_aarch64_irq_dispatch(
    os::kernel::aarch64::ExceptionFrame* frame) noexcept {
    if (frame == nullptr || !boot_gic.initialized) halt();
    const auto intid = os::kernel::aarch64::gic_v3_acknowledge();
    if (intid >= 1020U && intid <= 1023U) return;

    if (intid == boot_gic.device_intid) {
        // Routed straight through Kernel::dispatch_interrupt(), never through
        // PreemptionCoordinator - a device asserting is not the scheduling
        // decision point the timer's own PPI is, so it must not reuse that
        // path. The only caller of interrupt_attach in this boot proof is
        // process A's redirected resume (see interrupt_attach_entry_virtual
        // and complete_after_switch), which also arms the stand-in device
        // that raises this line - see arm_stand_in_device_source.
        //
        // Disarmed immediately, before dispatch: this comparator's condition
        // never clears itself the way a real device's would once serviced -
        // see disarm_stand_in_device_source - so without this the line would
        // reassert the instant it is unmasked, indefinitely. A real device
        // source needs no equivalent step here; this exists only because a
        // comparator is standing in for one.
        disarm_stand_in_device_source();
        auto dispatched = boot_kernel.dispatch_interrupt(boot_device_interrupt_source);
        if (!dispatched) {
            uart_write("COOKIE:PANIC:INTERRUPT_DISPATCH\n");
            halt();
        }
        uart_write("COOKIE:M7.9:DISPATCHED\n");
        // Masked until interrupt_complete unmasks it. True for an owned
        // source because dispatch() just moved it out of `attached`, and
        // true for a spurious one too (dispatched.value().owner is
        // invalid_thread: nobody has attached) - InterruptTable has no slot
        // to ask in that case, is_masked() would only return not_attached,
        // and leaving an asserting line enabled with no driver behind it is
        // a livelock at the controller regardless of whether InterruptTable
        // has anyone to charge it to. So this masks unconditionally rather
        // than consulting InterruptTable's state first.
        os::kernel::aarch64::gic_v3_set_ppi_masked(boot_gic.redistributor, intid, true);
        os::kernel::aarch64::gic_v3_end_interrupt(intid);
        return;
    }

    if (intid != boot_gic.timer_intid) {
        uart_write("COOKIE:PANIC:UNEXPECTED_IRQ\n");
        halt();
    }

    const auto running = boot_preemption.running();
    if ((running == process_a_thread && frame->x[19] != process_a_marker) ||
        (running == process_b_thread && frame->x[19] != process_b_marker) ||
        (running != process_a_thread && running != process_b_thread)) {
        // Say which of the three ways this failed, and with what values. The
        // first occurrence of this panic reported the name and nothing else,
        // and identifying it as a first-instruction race in B cost a reading
        // of the whole proof to work out which disjunct could even fire. The
        // three are not one bug: a wrong marker means register state did not
        // survive a switch, and an unexpected thread means the scheduler
        // resumed something this proof never admitted.
        uart_write("COOKIE:PANIC:UNIVERSE_MARKER");
        if (running != process_a_thread && running != process_b_thread) {
            uart_write(":UNEXPECTED_THREAD");
        } else {
            uart_write(running == process_a_thread ? ":A" : ":B");
            uart_write(" want=");
            uart_write_hex(running == process_a_thread ? process_a_marker : process_b_marker);
        }
        uart_write(" thread=");
        uart_write_hex(static_cast<std::uint64_t>(running));
        uart_write(" x19=");
        uart_write_hex(frame->x[19]);
        uart_write(" elr=");
        uart_write_hex(frame->elr_el1);
        uart_write("\n");
        halt();
    }

    ++timer_irq_count;

    // Preserve the completed A-B-A timer proof, then turn the exact saved A
    // frame into the IPC client continuation before it is captured.
    if (timer_irq_count == 3U && running == process_a_thread) {
        frame->elr_el1 = ipc_entry_virtual;
        frame->x[0] = boot_client_ipc_cap;
        frame->x[1] = ipc_client_exchange;
        frame->x[2] = ipc_request_size;
        frame->x[8] = static_cast<std::uint64_t>(os::kernel::KernelCall::send);
    }

    const auto delivered = boot_preemption.current_deadline();
    const auto now = os::kernel::machine_monotonic_nanoseconds();

    // Drain before choosing. A waiter whose bounded receive has run out is
    // runnable, and the scheduler must see it as runnable in this decision
    // rather than the next one - otherwise the wakeup this timer exists for is
    // deferred by a whole slice. A loop rather than a single take because
    // several deadlines can fall inside one timer grain, and leaving the later
    // ones armed would need another interrupt to collect what is already due.
    for (;;) {
        auto expired = boot_kernel.ipc_expire_one_receive(now);
        if (!expired) {
            uart_write("COOKIE:PANIC:IPC_EXPIRY\n");
            halt();
        }
        if (!expired.value()) break;
    }

    auto next = boot_preemption.on_timer(
        boot_kernel.runqueue(), boot_translations, boot_epochs, delivered, now, *frame,
        boot_kernel.ipc_earliest_receive_deadline());
    if (!next || !commit_result(next.value(), now) ||
        !complete_after_switch(next.value().next, *frame)) {
        uart_write("COOKIE:PANIC:PREEMPT\n");
        halt();
    }

    os::kernel::aarch64::gic_v3_end_interrupt(intid);

    if (timer_irq_count == 1U) {
        if (running != process_a_thread || next.value().next != process_b_thread ||
            !next.value().switched) halt();
        uart_write("COOKIE:M7.5g:TIMER_IRQ\n");
        uart_write("COOKIE:M7.5i:A_TO_B\n");
    } else if (timer_irq_count == 2U) {
        if (running != process_b_thread || next.value().next != process_a_thread ||
            !next.value().switched) halt();
        uart_write("COOKIE:M7.5g:TIMER_IRQ_RETURN\n");
        uart_write("COOKIE:M7.5i:B_TO_A\n");
    } else if (timer_irq_count == 3U) {
        if (running != process_a_thread || next.value().next != process_b_thread ||
            !next.value().switched) halt();
        uart_write("COOKIE:M7.5i:ISOLATED_A_B_A\n");

        // B enters receive first. It will block, return execution to A's saved
        // client frame, and later finish the original receive only after B's
        // root/ASID has been reinstalled by A's send wakeup.
        frame->elr_el1 = ipc_entry_virtual;
        frame->x[0] = boot_server_ipc_cap;
        frame->x[1] = ipc_server_exchange;
        frame->x[2] = 0U;
        frame->x[8] = static_cast<std::uint64_t>(os::kernel::KernelCall::receive);
        uart_write("COOKIE:M7.6:IPC_RECEIVER_FIRST\n");
    }
}

extern "C" [[noreturn]] void cookie_aarch64_boot_main(std::uintptr_t dtb_physical) noexcept {
    // Vectors first, before any code that can fault. VBAR_EL1 has no
    // architectural reset value, so before this write a synchronous fault
    // vectors to physical 0x200 - flash on this board, not a handler - and the
    // machine executes flash. That is how the M7.5d boot fault stayed
    // invisible: nothing halted, because nothing reached a halt.
    //
    // They stay valid across both MMU transitions below because both identity
    // mappings keep the address they point at.
    if (!os::kernel::aarch64::install_exception_vectors()) halt_no_vectors();

    const auto dtb_blob = bounded_dtb(dtb_physical);
    if (dtb_blob.empty()) halt_no_dtb();

    // The DTB's exact physical extent is already known here, safely:
    // bounded_dtb read only the header's magic and total-size fields, four
    // bytes at a time through read_be32, which never performs an unaligned
    // access regardless of the blob's own alignment. FdtView::parse below
    // does not have that property - it walks the blob as typed structures,
    // the access pattern Device-nGnRnE memory (what every access is under
    // with translation off) forbids unaligned - so translation has to be
    // live before that call runs, not after it.
    //
    // What to map for that is known for the same reason: the kernel's own
    // image bounds are link-time constants, and the DTB's bounds just came
    // out of a parse that is safe with translation off. TF-A and Linux solve
    // the equivalent problem with a conservative fixed window, generous
    // enough to cover a DTB they have not yet measured - Cookie does not
    // need one, because bounded_dtb already measured this one.
    const auto image_begin = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(__cookie_image_start));
    const auto image_end = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(__cookie_image_end));
    std::uint64_t dtb_end = 0ULL;
    if (dtb_physical > UINT64_MAX - dtb_blob.size() ||
        !align_up(static_cast<std::uint64_t>(dtb_physical) + dtb_blob.size(), dtb_end)) {
        halt_early_identity_map();
    }

    const HardwareRange image_range{align_down(image_begin), image_end - align_down(image_begin)};
    const HardwareRange dtb_range{
        align_down(static_cast<std::uint64_t>(dtb_physical)),
        dtb_end - align_down(static_cast<std::uint64_t>(dtb_physical))};
    if (!image_range.valid() || !dtb_range.valid()) halt_early_identity_map();

    // A minimal, throwaway identity map covering exactly the kernel's own
    // image (by segment, so .text/.rodata/.data+.bss keep the same
    // permissions the real map gives them further down) and the DTB's exact
    // extent - nothing else, since nothing else is known yet. Its page
    // tables live in early_identity_table_storage, a small static buffer
    // with no discovery dependency, distinct from plan.value().page_tables
    // below (which cannot exist yet: it comes from the boot memory plan,
    // which comes from the inventory, which comes from the parse this map
    // exists to protect).
    //
    // "Minimal" is load-bearing and also a hazard: this map is the entire
    // address space from the activation below until the real one replaces
    // it, so every physical region boot touches in that span must be added
    // to it as it becomes known - see extend_early_identity_map's call
    // sites. It gets its own ledger for the reason recorded at
    // early_identity_ledger's declaration.
    const auto early_identity_storage_begin = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(early_identity_table_storage));
    os::kernel::aarch64::EarlyPageArena early_identity_arena{
        early_identity_storage_begin,
        early_identity_storage_begin + early_identity_table_pages * page_size};
    if (!early_identity_arena.valid()) halt_early_identity_map();
    os::kernel::aarch64::EarlyStage1Builder early_identity_builder{early_identity_arena};
    auto early_identity_root = early_identity_builder.initialize();
    if (!early_identity_root) halt_early_identity_map();

    if (!os::kernel::machine_bind_address_space(early_identity_space, early_identity_ledger)) {
        halt_early_identity_map();
    }
    if (!os::kernel::aarch64_attach_early_stage1(early_identity_space, early_identity_builder)) {
        halt_early_identity_map();
    }

    os::kernel::aarch64::KernelMappingManifest early_identity_manifest{};
    if (!add_identity_symbols(
            early_identity_manifest, __cookie_text_start, __cookie_text_end,
            MachinePermissions::read_execute) ||
        !add_identity_symbols(
            early_identity_manifest, __cookie_rodata_start, __cookie_rodata_end,
            MachinePermissions::read) ||
        !add_identity_symbols(
            early_identity_manifest, __cookie_data_start, __bss_end,
            MachinePermissions::read_write)) {
        halt_early_identity_map();
    }
    if (!os::kernel::aarch64::replay_kernel_mapping_manifest(
            early_identity_manifest, early_identity_space)) {
        halt_early_identity_map();
    }
    if (!os::kernel::machine_map(
            early_identity_space,
            static_cast<std::uintptr_t>(dtb_range.base),
            static_cast<std::uintptr_t>(dtb_range.base),
            static_cast<std::size_t>(dtb_range.size),
            MachinePermissions::read,
            MachineMemoryKind::normal)) {
        halt_early_identity_map();
    }

    // First of two activations. The second, much further down, replaces
    // TTBR0_EL1 wholesale with the full discovered mapping once one exists;
    // both are safe because both keep the currently-executing image (code,
    // rodata, globals, stack) mapped identically, and
    // activate_stage1_translation's own MAIR/TCR/TTBR programming has no
    // dependency on anything this call has not already established.
    if (!os::kernel::aarch64::activate_stage1_translation(early_identity_root.value())) {
        halt_early_mmu();
    }

    auto fdt = os::kernel::FdtView::parse(dtb_blob);
    if (!fdt) halt_unparsable_fdt();
    auto inventory = os::kernel::discover_hardware(fdt.value());
    if (!inventory) halt_inventory_error(inventory.error());

    // Publish the UART the moment the device tree names it. Translation is
    // still off, so the physical address is directly addressable, and every
    // failure from here on becomes a reportable stage rather than a silent
    // halt. Only the checks above remain mute, which is unavoidable: until the
    // device tree has been read there is no discovered console to report
    // through, and hardware neutrality forbids assuming one.
    //
    // This is where the M7.5e-M7.7 chain regressed. The merge kept the code
    // that finds the console but moved the assignment down beside the MMU
    // activation, past thirty new failure points that the milestones added.
    // Every one of them then halted before anything could be printed, and the
    // whole boot presented as a QEMU timeout with an empty log.
    const auto* uart = find_pl011(inventory.value());
    if (uart == nullptr || !uart->registers.valid()) halt_no_console();
    // The console must enter the early identity map before it can be written
    // through. Translation is on now, so unlike every previous version of
    // this sequence the discovered physical address is not automatically
    // addressable - and a console that faults is worse than no console: the
    // fault handler prints too, so its own uart_write takes the same abort
    // and the machine loops in the vector reporting nothing at all.
    if (!extend_early_identity_map(
            uart->registers, MachinePermissions::read_write,
            MachineMemoryKind::device)) halt_no_console();
    boot_uart = reinterpret_cast<volatile std::uint32_t*>(
        static_cast<std::uintptr_t>(uart->registers.base));

    // Checked after the console rather than beside the discovery call. A walk
    // that succeeds but yields no usable RAM is a different failure from a
    // walk that could not complete, and it is the one that can be reported.
    if (inventory.value().memory_count == 0U) fail("NO_MEMORY_NODE");

    auto gic_topology = os::kernel::discover_gic_v3(fdt.value());
    if (!gic_topology) fail("NO_GICV3_NODE");
    auto timer = os::kernel::discover_architected_timer(fdt.value());
    if (!timer) fail("NO_TIMER_NODE");
    if ((timer.value().trigger_flags & 0xFU) != 4U) fail("TIMER_TRIGGER");
    if ((timer.value().virtual_trigger_flags & 0xFU) != 4U) fail("TIMER_VIRT_TRIGGER");
    if (timer.value().virtual_intid == timer.value().nonsecure_physical_intid) {
        fail("TIMER_VIRT_ALIAS");
    }

    // image_range and dtb_range were computed above, before FdtView::parse,
    // to build the early identity map - reused here rather than recomputed.
    const std::array<HardwareRange, 2U> protected_ranges{image_range, dtb_range};
    auto plan = os::kernel::plan_early_boot_memory(
        inventory.value(), std::span<const HardwareRange>{protected_ranges},
        early_page_table_pages, runtime_stack_pages, page_size);
    if (!plan || !plan.value().valid()) fail("MEMORY_PLAN");

    const std::array<HardwareRange, 4U> a_code_protected{
        image_range, dtb_range, plan.value().page_tables, plan.value().kernel_stack};
    auto a_code = os::kernel::select_early_ram(
        inventory.value(), page_size, page_size,
        std::span<const HardwareRange>{a_code_protected});
    if (!a_code) fail("RAM_A_CODE");

    const std::array<HardwareRange, 5U> a_stack_protected{
        image_range, dtb_range, plan.value().page_tables, plan.value().kernel_stack,
        a_code.value()};
    auto a_stack = os::kernel::select_early_ram(
        inventory.value(), page_size, page_size,
        std::span<const HardwareRange>{a_stack_protected});
    if (!a_stack) fail("RAM_A_STACK");

    const std::array<HardwareRange, 6U> b_code_protected{
        image_range, dtb_range, plan.value().page_tables, plan.value().kernel_stack,
        a_code.value(), a_stack.value()};
    auto b_code = os::kernel::select_early_ram(
        inventory.value(), page_size, page_size,
        std::span<const HardwareRange>{b_code_protected});
    if (!b_code) fail("RAM_B_CODE");

    const std::array<HardwareRange, 7U> b_stack_protected{
        image_range, dtb_range, plan.value().page_tables, plan.value().kernel_stack,
        a_code.value(), a_stack.value(), b_code.value()};
    auto b_stack = os::kernel::select_early_ram(
        inventory.value(), page_size, page_size,
        std::span<const HardwareRange>{b_stack_protected});
    if (!b_stack) fail("RAM_B_STACK");

    // Pages for the address space M7.11's proof builds after boot. Four for
    // translation tables (a root, and the level-2 and level-3 tables one
    // mapping needs, plus one spare) and a fifth to be the page that mapping
    // points at, so the proof maps memory that is not itself a table.
    //
    // Selected here rather than carved out of plan.value().page_tables on
    // purpose: the point of the exercise is that a space created after boot
    // gets its pages from a caller, not from an arena the boot plan sized.
    const std::array<HardwareRange, 8U> dynamic_protected{
        image_range, dtb_range, plan.value().page_tables, plan.value().kernel_stack,
        a_code.value(), a_stack.value(), b_code.value(), b_stack.value()};
    auto dynamic_pages = os::kernel::select_early_ram(
        inventory.value(), dynamic_proof_pages * page_size, page_size,
        std::span<const HardwareRange>{dynamic_protected});
    if (!dynamic_pages) fail("RAM_DYNAMIC");

    // Everything selected out of discovered RAM above is still outside the
    // early identity map, and all of it is written before the real mapping
    // takes over: the four process pages by the install/zero/write calls
    // immediately below, the page-table region by the builders after them.
    // Mapped writable here even though a_code and b_code end up read-execute
    // in the real map - installing a program is a write, and executing it
    // cannot happen until a map that is not this one is live.
    if (!extend_early_identity_map(
            plan.value().page_tables, MachinePermissions::read_write,
            MachineMemoryKind::normal)) fail("EARLY_MAP_TABLES");
    if (!extend_early_identity_map(
            a_code.value(), MachinePermissions::read_write,
            MachineMemoryKind::normal)) fail("EARLY_MAP_A_CODE");
    if (!extend_early_identity_map(
            a_stack.value(), MachinePermissions::read_write,
            MachineMemoryKind::normal)) fail("EARLY_MAP_A_STACK");
    if (!extend_early_identity_map(
            b_code.value(), MachinePermissions::read_write,
            MachineMemoryKind::normal)) fail("EARLY_MAP_B_CODE");
    if (!extend_early_identity_map(
            b_stack.value(), MachinePermissions::read_write,
            MachineMemoryKind::normal)) fail("EARLY_MAP_B_STACK");
    // Writable here for the same reason a_code is: EarlyStage1Builder writes a
    // translation table through its raw physical address, so the pages donated
    // below have to be writable in whichever map is live while they are built,
    // and this is that map.
    //
    // This relies on the two ledgers being separate, which they are, and
    // deliberately - see early_identity_ledger's declaration. The reservation
    // the donation takes lives in boot_physical_ledger and cannot see this
    // mapping. That is sound for exactly the reason the separation is: the two
    // maps are sequential rather than concurrent, this one is kernel-only, and
    // it is abandoned before any process runs, so no EL0 translation of a
    // table page ever exists.
    if (!extend_early_identity_map(
            dynamic_pages.value(), MachinePermissions::read_write,
            MachineMemoryKind::normal)) fail("EARLY_MAP_DYNAMIC");

    install_process_a_program(a_code.value().base);
    install_process_b_program(b_code.value().base);
    zero_page(a_stack.value().base);
    zero_page(b_stack.value().base);
    const std::array<std::byte, ipc_request_size> request_bytes{
        std::byte{'C'}, std::byte{'O'}, std::byte{'O'},
        std::byte{'K'}, std::byte{'I'}, std::byte{'E'}};
    const std::array<std::byte, ipc_response_size> response_bytes{
        std::byte{'O'}, std::byte{'K'}};
    write_physical_bytes(
        a_stack.value().base,
        static_cast<std::size_t>(ipc_exchange_offset),
        std::span<const std::byte>{request_bytes});
    write_physical_bytes(
        b_stack.value().base,
        static_cast<std::size_t>(ipc_response_offset),
        std::span<const std::byte>{response_bytes});

    os::kernel::aarch64::EarlyPageArena arena{
        plan.value().page_tables.base,
        plan.value().page_tables.end_exclusive()};
    if (!arena.valid()) fail("PAGE_ARENA");
    os::kernel::aarch64::EarlyStage1Builder boot_builder{arena};
    os::kernel::aarch64::EarlyStage1Builder builder_a{arena};
    os::kernel::aarch64::EarlyStage1Builder builder_b{arena};
    auto boot_root = boot_builder.initialize();
    if (!boot_root) fail("TABLE_ROOT_BOOT");
    if (!builder_a.initialize()) fail("TABLE_ROOT_A");
    if (!builder_b.initialize()) fail("TABLE_ROOT_B");

    if (!os::kernel::machine_bind_address_space(boot_kernel_space, boot_physical_ledger) ||
        !os::kernel::machine_bind_address_space(process_space_a, boot_physical_ledger) ||
        !os::kernel::machine_bind_address_space(process_space_b, boot_physical_ledger)) {
        fail("BIND_SPACE");
    }
    if (!os::kernel::aarch64_attach_early_stage1(boot_kernel_space, boot_builder) ||
        !os::kernel::aarch64_attach_early_stage1(process_space_a, builder_a) ||
        !os::kernel::aarch64_attach_early_stage1(process_space_b, builder_b)) {
        fail("ATTACH_STAGE1");
    }

    // Everything below runs before the manifest replays and before anything is
    // mapped, so these are properties the ledger enforces on every later map
    // rather than properties of the order this routine happens to do things in.
    //
    // The arena the three builders draw from is where every translation table on
    // this machine lives, including the two processes' own, and exactly one
    // space edits it.
    if (!os::kernel::aarch64_reserve_physical(
            boot_kernel_space,
            static_cast<std::uintptr_t>(plan.value().page_tables.base),
            static_cast<std::size_t>(plan.value().page_tables.size),
            os::kernel::aarch64::PhysicalReservationKind::kernel_object)) {
        fail("RESERVE_TABLES");
    }

    // The kernel's writable image and its stack. This is where boot_kernel's
    // capability table, boot_physical_ledger, boot_epochs, boot_translations and
    // every saved exception frame actually live, so an EL0 translation of these
    // ranges reads other processes' register state at best and rewrites the
    // authority tables at worst - and nothing refused one: the pre-existing W^X
    // check sees two writable mappings and no executable one, which is not a
    // violation it knows about.
    //
    // kernel_private rather than kernel_object because these must stay writable
    // in all three spaces. Cookie translates through TTBR0 only, so EL1 executes
    // under whichever process root is installed; owner-write-only here would
    // stop the kernel running the moment a process was scheduled. M7.7's TTBR1
    // split is what makes the stronger kind available for them.
    if (!os::kernel::aarch64_reserve_physical(
            boot_kernel_space,
            reinterpret_cast<std::uintptr_t>(__cookie_data_start),
            static_cast<std::size_t>(
                reinterpret_cast<std::uintptr_t>(__bss_end) -
                reinterpret_cast<std::uintptr_t>(__cookie_data_start)),
            os::kernel::aarch64::PhysicalReservationKind::kernel_private)) {
        fail("RESERVE_KERNEL_DATA");
    }
    if (!os::kernel::aarch64_reserve_physical(
            boot_kernel_space,
            static_cast<std::uintptr_t>(plan.value().kernel_stack.base),
            static_cast<std::size_t>(plan.value().kernel_stack.size),
            os::kernel::aarch64::PhysicalReservationKind::kernel_private)) {
        fail("RESERVE_KERNEL_STACK");
    }

    os::kernel::aarch64::KernelMappingManifest kernel_manifest{};
    if (!add_identity_symbols(
            kernel_manifest, __cookie_text_start, __cookie_text_end,
            MachinePermissions::read_execute) ||
        !add_identity_symbols(
            kernel_manifest, __cookie_rodata_start, __cookie_rodata_end,
            MachinePermissions::read) ||
        !add_identity_symbols(
            kernel_manifest, __cookie_data_start, __bss_end,
            MachinePermissions::read_write) ||
        !add_manifest_range(
            kernel_manifest,
            runtime_stack_virtual,
            plan.value().kernel_stack.base,
            plan.value().kernel_stack.size,
            MachinePermissions::read_write,
            MachineMemoryKind::normal,
            os::kernel::aarch64::KernelMappingRole::guarded_stack) ||
        // The page EL0 will name as its created space's root. It has to be in
        // the manifest, which means mapped in all three spaces, because the
        // kernel builds that space's tables from inside A's syscall - and
        // Cookie translates through TTBR0 only, so EL1 is running under A's
        // root at that moment. A mapping in boot_kernel_space alone would not
        // be there when it is needed.
        //
        // Kernel-only and writable. It is also why donation reserves
        // kernel_private: three kernel-side writers of a page that is about to
        // become a translation table is exactly what kernel_object forbids and
        // what TTBR0-only translation forces.
        !add_manifest_range(
            kernel_manifest,
            dynamic_pages.value().base + (dynamic_proof_pages - 1U) * page_size,
            dynamic_pages.value().base + (dynamic_proof_pages - 1U) * page_size,
            page_size,
            MachinePermissions::read_write,
            MachineMemoryKind::normal) ||
        !add_device_manifest(kernel_manifest, gic_topology.value().distributor) ||
        !add_device_manifest(kernel_manifest, uart->registers)) fail("MANIFEST");

    for (std::size_t i = 0U; i < gic_topology.value().redistributor_count; ++i) {
        if (!add_device_manifest(kernel_manifest, gic_topology.value().redistributors[i])) {
            fail("MANIFEST_REDIST");
        }
    }

    // Split, not chained. The three replays draw page-table pages from one
    // shared arena, so the first to exhaust it is the interesting one - and a
    // single || chain reports all eleven of these as the same nothing.
    if (!os::kernel::aarch64::replay_kernel_mapping_manifest(kernel_manifest, boot_kernel_space)) {
        fail("REPLAY_KERNEL");
    }
    if (!os::kernel::aarch64::replay_kernel_mapping_manifest(kernel_manifest, process_space_a)) {
        fail("REPLAY_A");
    }
    if (!os::kernel::aarch64::replay_kernel_mapping_manifest(kernel_manifest, process_space_b)) {
        fail("REPLAY_B");
    }
    if (!os::kernel::machine_map(
            boot_kernel_space,
            static_cast<std::uintptr_t>(plan.value().page_tables.base),
            static_cast<std::uintptr_t>(plan.value().page_tables.base),
            static_cast<std::size_t>(plan.value().page_tables.size),
            MachinePermissions::read_write,
            MachineMemoryKind::normal)) fail("MAP_TABLES");
    if (!os::kernel::machine_map(
            boot_kernel_space,
            static_cast<std::uintptr_t>(dtb_range.base),
            static_cast<std::uintptr_t>(dtb_range.base),
            static_cast<std::size_t>(dtb_range.size),
            MachinePermissions::read,
            MachineMemoryKind::normal)) fail("MAP_DTB");
    if (!os::kernel::aarch64_map_user(
            process_space_a,
            static_cast<std::uintptr_t>(user_code_virtual),
            static_cast<std::uintptr_t>(a_code.value().base),
            static_cast<std::size_t>(page_size),
            MachinePermissions::read_execute)) fail("MAP_A_CODE");
    if (!os::kernel::aarch64_map_user_stack(
            process_space_a,
            static_cast<std::uintptr_t>(user_stack_virtual),
            static_cast<std::uintptr_t>(a_stack.value().base),
            static_cast<std::size_t>(page_size))) fail("MAP_A_STACK");
    if (!os::kernel::aarch64_map_user(
            process_space_b,
            static_cast<std::uintptr_t>(user_code_virtual),
            static_cast<std::uintptr_t>(b_code.value().base),
            static_cast<std::size_t>(page_size),
            MachinePermissions::read_execute)) fail("MAP_B_CODE");
    if (!os::kernel::aarch64_map_user_stack(
            process_space_b,
            static_cast<std::uintptr_t>(user_stack_virtual),
            static_cast<std::uintptr_t>(b_stack.value().base),
            static_cast<std::size_t>(page_size))) fail("MAP_B_STACK");

    auto sealed_a = os::kernel::aarch64::TranslationRootSealer::seal(builder_a);
    auto sealed_b = os::kernel::aarch64::TranslationRootSealer::seal(builder_b);
    auto epoch_a = boot_epochs.acquire();
    auto epoch_b = boot_epochs.acquire();
    if (!sealed_a || !sealed_b || !epoch_a || !epoch_b ||
        !boot_translations.bind(process_a_thread, epoch_a.value(), sealed_a.value(), boot_epochs) ||
        !boot_translations.bind(process_b_thread, epoch_b.value(), sealed_b.value(), boot_epochs) ||
        !boot_kernel.create_thread(process_a_thread, process_priority) ||
        !boot_kernel.create_thread(process_b_thread, process_priority)) fail("SEAL_BIND");

    auto endpoint = boot_kernel.create_ipc_endpoint(process_b_thread);
    if (!endpoint) fail("IPC_ENDPOINT");
    auto server_cap = boot_kernel.capabilities().mint(
        process_b_thread,
        os::kernel::ipc_object_id(endpoint.value()),
        os::kernel::ipc_right_all,
        true);
    if (!server_cap) fail("IPC_SERVER_CAP");
    auto client_cap = boot_kernel.capabilities().grant(
        process_b_thread,
        server_cap.value(),
        process_a_thread,
        os::kernel::ipc_right_send,
        false);
    if (!client_cap) fail("IPC_CLIENT_CAP");
    boot_server_ipc_cap = server_cap.value();
    boot_client_ipc_cap = client_cap.value();

    // The M7.9 proof's own capability, minted the same way the IPC ones just
    // above were - held by A, not context-bound (the legacy ThreadId-only
    // path M7.6a's own IPC syscalls launched on; M7.9's design doc leaves
    // moving to M7.8 execution-authority binding as a later, undecided step).
    auto device_cap = boot_kernel.capabilities().mint(
        process_a_thread,
        os::kernel::interrupt_object_id(boot_device_interrupt_source),
        os::kernel::interrupt_right_attach,
        false);
    if (!device_cap) fail("INTERRUPT_CAP");
    boot_device_interrupt_cap = device_cap.value();

    if (!boot_kernel.runqueue().update(process_b_thread, false, process_priority)) fail("RUNQUEUE");

    os::kernel::aarch64::ExceptionFrame initial_a{};
    initial_a.elr_el1 = user_code_virtual;
    initial_a.sp_el0 = user_stack_virtual + page_size;
    initial_a.spsr_el1 = 0x340ULL;
    os::kernel::aarch64::ExceptionFrame initial_b = initial_a;

    // B's universe marker is established here, before B has run an
    // instruction, rather than only by the movz at word 0 of its program.
    //
    // This closes a real race that made kernel-arm64-native fail
    // intermittently with COOKIE:PANIC:UNIVERSE_MARKER and pass on a bare
    // re-run. B is first entered by frame restore from inside the timer
    // handler, and that same handler arms the next deadline before it
    // returns - so the timer can assert again between the ERET into B and
    // the retirement of B's first instruction. In that window B is the
    // running thread with x19 still holding the zero its admitted frame
    // carried, and the marker check below fails on a kernel that is working
    // exactly as intended.
    //
    // A deliberately does not get the same treatment. Its marker is
    // load-bearing twice in the syscall path above - the yield-3 gate and
    // the contention gate both require frame->x[19] to equal it - and those
    // checks exist to prove A's own movz executed and survived two syscall
    // round trips. Seeding A here would make both vacuous. A needs no seed
    // anyway: nothing arms a deadline before its first instruction, which is
    // asserted at the EL0 start below (start.value().deadline.active must be
    // false), so A's pre-marker window is unreachable by a timer.
    //
    // B's own movz stays. It is now a re-assertion rather than the first
    // write, and nothing gates on B having executed it - the isolation this
    // proves is that a switch keeps the two threads' x19 distinct, which is
    // a property of the saved frames and is tested exactly as before.
    initial_b.x[19] = process_b_marker;

    if (!boot_preemption.admit_frame(process_a_thread, initial_a) ||
        !boot_preemption.admit_frame(process_b_thread, initial_b)) fail("ADMIT_FRAME");

    // M7.11: an address space created, authorized, destroyed, and its
    // capability refused afterwards - on the machine rather than on the host.
    //
    // Everything M7.11 has landed so far is host-tested, and this milestone's
    // exit criteria are written in terms of a running system. This proves the
    // part that can be proved today: the kernel's decision about who may create
    // and destroy a space, and the two-phase retirement, running under real
    // translation with a real ledger.
    //
    // It deliberately does not donate a page or map anything, so it does not
    // prove the whole of "creation after boot". That is not an oversight and
    // the reason is worth recording where the next person will look. A donated
    // table page must be reserved as kernel_object owned by the new space,
    // while EarlyStage1Builder writes tables through their raw physical
    // address - which under live translation needs that page mapped writable in
    // the *active* space, which is not the new one. forbidden_by_reservation
    // refuses exactly that combination, and it is right to: the rule is what
    // stops a donor keeping write access to what has become a translation
    // table. Making the proof pass by relaxing it would be weakening a
    // security rule to fit a demo. The ordering that satisfies both is a design
    // decision this proof does not need and should not pre-empt.
    auto address_space_authority = boot_kernel.capabilities().mint(
        process_a_thread,
        os::kernel::address_space_authority_object,
        os::kernel::address_space_right_create,
        false);
    if (!address_space_authority) fail("M7_11_AUTHORITY");
    // Held for the EL0 proof further down, which needs the same authority from
    // inside a syscall rather than from here.
    el0_space_authority = address_space_authority.value();
    // The last of the pages selected for this proof, kept back from the
    // donations above so EL0 has one to name as its own space's root.
    // The page A is given authority over, and the capability naming that
    // authority. Boot is the origin of every grant because boot is the only
    // thing that has seen the memory map - a process cannot grant itself
    // memory, which is the property the whole scheme rests on.
    auto root_granted = boot_grants.create(
        dynamic_pages.value().base + (dynamic_proof_pages - 1U) * page_size,
        page_size);
    if (!root_granted) fail("M7_11_GRANT");
    // memory_right_donate, not memory_right_map: A is being given a page to
    // spend on a kernel object, and it never maps this one itself.
    auto root_grant_capability = boot_kernel.capabilities().mint(
        process_a_thread,
        os::kernel::memory_grant_object_id(root_granted.value().identity),
        os::kernel::memory_right_donate,
        false);
    if (!root_grant_capability) fail("M7_11_GRANT_CAP");
    el0_space_root_grant = root_grant_capability.value();

    // The region A's fault probe lands in. Declared `paged`, which is the
    // disclosure class that permits a pager to be asked at all - `sealed`
    // would refuse to produce a question and terminate instead, which is the
    // right default for key material and the wrong thing to demonstrate a
    // report with.
    // The page B hands back when asked, and B's claim on it. Granted from boot
    // because boot is the only origin of grants - B cannot grant itself the
    // memory it pages with, which is the property that makes a pager a
    // supplier rather than an authority.
    boot_pager_backing_page =
        dynamic_pages.value().base + (dynamic_proof_pages - 2U) * page_size;
    auto pager_granted = boot_grants.create(boot_pager_backing_page, page_size);
    if (!pager_granted) fail("M7_11_PAGER_GRANT");
    // memory_right_map, not donate: B supplies memory to be mapped for someone
    // else. It is never spending it on a kernel object, and giving it the
    // stronger right would be authority the pager role does not need.
    auto pager_backing_capability = boot_kernel.capabilities().mint(
        boot_pager_thread,
        os::kernel::memory_grant_object_id(pager_granted.value().identity),
        os::kernel::memory_right_map,
        false);
    if (!pager_backing_capability) fail("M7_11_PAGER_CAP");
    boot_pager_backing_cap = pager_backing_capability.value();

    if (!boot_fault_regions.declare(
            fault_probe_virtual, page_size,
            os::kernel::FaultDisclosure::paged)) fail("M7_11_DECLARE_REGION");

    if (!os::kernel::aarch64_create_address_space(
            dynamic_space, boot_physical_ledger, dynamic_builder)) fail("M7_11_CREATE");
    auto dynamic_created = boot_kernel.address_space_create(
        process_a_thread, address_space_authority.value(), boot_epochs);
    if (!dynamic_created || !dynamic_created.value().valid()) fail("M7_11_AUTHORIZE");
    if (!boot_epochs.active(dynamic_created.value().epoch)) fail("M7_11_NOT_ACTIVE");
    uart_write("COOKIE:M7.11:SPACE_CREATED\n");

    // The space has no pages of its own, so the caller supplies them. This is
    // the no-allocator decision made concrete: the kernel cannot answer "give
    // me a page" and never has to, because the only way to get one is for
    // whoever already holds it to hand it over.
    for (std::size_t page = 0U; page < dynamic_donated_pages; ++page) {
        if (!os::kernel::aarch64_donate_table_page(
                dynamic_space, dynamic_arena,
                static_cast<std::uintptr_t>(
                    dynamic_pages.value().base + page * page_size))) fail("M7_11_DONATE");
    }
    // Refuses with `exhausted` before anything is donated, which is a caller
    // error the caller can fix - never a kernel failure the caller can do
    // nothing about. That is the property the no-allocator decision buys, so
    // the root is built only after the donations above.
    if (!os::kernel::aarch64_initialize_translation_root(dynamic_space)) {
        fail("M7_11_ROOT");
    }
    // The last page, mapped user-accessible into the created space. Not one of
    // the donated pages: those are reserved kernel_object, and a user-readable
    // translation of a live translation table is precisely what that
    // reservation exists to refuse.
    if (!os::kernel::aarch64_map_user(
            dynamic_space,
            static_cast<std::uintptr_t>(dynamic_proof_virtual),
            static_cast<std::uintptr_t>(
                dynamic_pages.value().base + dynamic_donated_pages * page_size),
            static_cast<std::size_t>(page_size),
            MachinePermissions::read_write)) fail("M7_11_MAP");
    uart_write("COOKIE:M7.11:SPACE_MAPPED\n");

    // Establish what reclamation has to erase, before it runs. Checked rather
    // than assumed: if these pages were already zero the check after the
    // destroy would pass without reclamation doing anything, and would keep
    // passing if reclamation were removed. A proof that cannot fail proves
    // nothing.
    //
    // They are not zero because they are translation tables with live
    // descriptors in them - the root points at a level-2 table, which points
    // at a level-3 table, which points at the page mapped above. That is
    // exactly the content worth erasing: it is the destroyed space's layout.
    if (!any_nonzero_page(dynamic_pages.value().base, dynamic_donated_pages)) {
        fail("M7_11_TABLES_ALREADY_ZERO");
    }

    const auto dynamic_identity = dynamic_created.value().epoch.identity();
    auto dynamic_retiring = boot_kernel.address_space_begin_destroy(
        process_a_thread, dynamic_created.value().capability, boot_epochs);
    if (!dynamic_retiring) fail("M7_11_BEGIN_DESTROY");
    if (!os::kernel::aarch64_release_address_space(
            dynamic_space, dynamic_retiring.value())) fail("M7_11_RELEASE");
    if (!boot_kernel.address_space_complete_destroy(
            process_a_thread, dynamic_created.value().capability,
            dynamic_retiring.value(), boot_epochs)) fail("M7_11_COMPLETE_DESTROY");
    uart_write("COOKIE:M7.11:SPACE_DESTROYED\n");

    // The other half of the pair. Every page that held the destroyed space's
    // translation tables is now zero, so nothing it knew about its own layout
    // survives into whoever donates or receives these pages next.
    if (any_nonzero_page(dynamic_pages.value().base, dynamic_donated_pages)) {
        fail("M7_11_TABLES_NOT_ZEROED");
    }
    uart_write("COOKIE:M7.11:PAGES_ZEROED\n");

    // The exit criterion, on hardware. complete_destroy revoked the original
    // capability, so this mints a fresh one over the identity that space used
    // to have - which is what a holder who kept a reference across the destroy
    // effectively has. It must not name anything.
    auto stale_capability = boot_kernel.capabilities().mint(
        process_a_thread,
        os::kernel::address_space_object_id(dynamic_identity),
        os::kernel::address_space_right_destroy,
        false);
    if (!stale_capability) fail("M7_11_STALE_MINT");
    if (boot_kernel.address_space_begin_destroy(
            process_a_thread, stale_capability.value(), boot_epochs)) {
        fail("M7_11_STALE_ACCEPTED");
    }
    uart_write("COOKIE:M7.11:STALE_REFUSED\n");

    boot_uart = reinterpret_cast<volatile std::uint32_t*>(
        static_cast<std::uintptr_t>(uart->registers.base));

    // Vectors already installed at entry; reinstalling here would imply the
    // activation on this line ran without a handler, which is the arrangement
    // that made the M7.5d fault unreportable.
    //
    // Second activation, not first: the early identity map built before
    // FdtView::parse already turned translation on once, covering only the
    // kernel image and the DTB. This unconditionally overwrites TTBR0_EL1
    // with the full discovered mapping built above - safe because both maps
    // identity-map the currently-executing image (code, rodata, globals,
    // stack) identically, so nothing this function is still using moves.
    if (!os::kernel::aarch64::activate_stage1_translation(boot_root.value())) fail("ACTIVATE_MMU");
    uart_write("COOKIE:M7.5d:MMU\n");

    auto gic = os::kernel::aarch64::initialize_gic_v3_primary_cpu(
        gic_topology.value(),
        timer.value().nonsecure_physical_intid,
        timer.value().virtual_intid);
    if (!gic) {
        uart_write("COOKIE:PANIC:GICV3\n");
        halt();
    }
    boot_gic = gic.value();
    uart_write("COOKIE:M7.5g:GICV3\n");

    os::kernel::MachineContext bootstrap_context{};
    os::kernel::MachineContext runtime_context{};
    const auto stack_top = runtime_stack_virtual + plan.value().kernel_stack.size;
    if (!os::kernel::machine_prepare_context(
            runtime_context,
            boot_kernel_space,
            reinterpret_cast<std::uintptr_t>(&guarded_runtime_main),
            static_cast<std::uintptr_t>(stack_top))) fail("PREPARE_CONTEXT");

    os::kernel::machine_switch_context(bootstrap_context, runtime_context);
    halt();
}
