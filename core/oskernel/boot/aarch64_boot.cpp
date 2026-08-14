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
constexpr std::size_t runtime_stack_pages = 8U;
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

os::kernel::MachinePhysicalLedger boot_physical_ledger{};
os::kernel::MachineAddressSpace boot_kernel_space{};
os::kernel::MachineAddressSpace process_space_a{};
os::kernel::MachineAddressSpace process_space_b{};
os::kernel::AddressSpaceEpochAuthority boot_epochs{};
os::kernel::ProcessTranslationTable boot_translations{};
os::kernel::Kernel boot_kernel{};
os::kernel::aarch64::PreemptionCoordinator boot_preemption{};

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

extern "C" void cookie_kernel_syscall_entry(
    os::kernel::aarch64::ExceptionFrame* frame) noexcept {
    if (frame == nullptr) halt();
    auto call = os::kernel::decode_call(static_cast<std::uint16_t>(frame->x[8]));
    const auto current = boot_preemption.running();
    if (!call ||
        (call.value().authority != os::kernel::CallAuthority::unprivileged &&
         call.value().authority != os::kernel::CallAuthority::interrupt_control) ||
        (current != process_a_thread && current != process_b_thread)) {
        uart_write("COOKIE:PANIC:EL0_SYSCALL\n");
        halt();
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
        auto next = boot_preemption.reschedule(
            boot_kernel.runqueue(), boot_translations, boot_epochs, now, *frame);
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
        return;
    }

    if (call.value().call != os::kernel::KernelCall::yield || current != process_a_thread) {
        uart_write("COOKIE:PANIC:EL0_SYSCALL\n");
        halt();
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
            boot_kernel.runqueue(), boot_translations, boot_epochs, boot_start_now, *frame);
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
        uart_write("COOKIE:PANIC:UNIVERSE_MARKER\n");
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
    auto next = boot_preemption.on_timer(
        boot_kernel.runqueue(), boot_translations, boot_epochs, delivered, now, *frame);
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
    // They stay valid across the MMU transition because the identity mapping
    // built below keeps the address they point at.
    if (!os::kernel::aarch64::install_exception_vectors()) halt_no_vectors();

    const auto dtb_blob = bounded_dtb(dtb_physical);
    if (dtb_blob.empty()) halt_no_dtb();

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

    const auto image_begin = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(__cookie_image_start));
    const auto image_end = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(__cookie_image_end));
    std::uint64_t dtb_end = 0ULL;
    if (dtb_physical > UINT64_MAX - dtb_blob.size() ||
        !align_up(static_cast<std::uint64_t>(dtb_physical) + dtb_blob.size(), dtb_end)) {
        fail("DTB_RANGE");
    }

    const HardwareRange image_range{align_down(image_begin), image_end - align_down(image_begin)};
    const HardwareRange dtb_range{
        align_down(static_cast<std::uint64_t>(dtb_physical)),
        dtb_end - align_down(static_cast<std::uint64_t>(dtb_physical))};
    if (!image_range.valid() || !dtb_range.valid()) fail("IMAGE_RANGE");

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
    if (!boot_preemption.admit_frame(process_a_thread, initial_a) ||
        !boot_preemption.admit_frame(process_b_thread, initial_b)) fail("ADMIT_FRAME");

    boot_uart = reinterpret_cast<volatile std::uint32_t*>(
        static_cast<std::uintptr_t>(uart->registers.base));

    // Vectors already installed at entry; reinstalling here would imply the
    // activation on this line ran without a handler, which is the arrangement
    // that made the M7.5d fault unreportable.
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
