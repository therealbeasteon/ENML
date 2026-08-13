#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <os/core/span.hpp>
#include <os/kernel/aarch64_entry.hpp>
#include <os/kernel/aarch64_page_tables.hpp>
#include <os/kernel/aarch64_translation.hpp>
#include <os/kernel/boot_memory_plan.hpp>
#include <os/kernel/fdt.hpp>
#include <os/kernel/hardware_inventory.hpp>
#include <os/kernel/machine.hpp>
#include <os/kernel/machine_aarch64.hpp>

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
constexpr std::size_t early_page_table_pages = 96U;
constexpr std::size_t runtime_stack_pages = 8U;
constexpr std::uint64_t runtime_stack_virtual = 0x0000'007F'FEF0'0000ULL;

volatile std::uint32_t* boot_uart = nullptr;

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
    while ((boot_uart[fr_index] & tx_fifo_full) != 0U) {
        asm volatile("yield" ::: "memory");
    }
    boot_uart[0] = static_cast<std::uint32_t>(static_cast<unsigned char>(value));
}

void uart_write(std::string_view text) noexcept {
    for (char c : text) uart_write_char(c);
}

// Early boot has roughly fifteen ways to stop and, until this existed, one
// observable outcome for all of them: a silent WFE loop that CI could only
// report as a timeout. The stage name costs a string literal and turns "the
// kernel hung" into "the kernel rejected the memory plan".
[[noreturn]] void fail(std::string_view stage) noexcept {
    uart_write("COOKIE:PANIC:");
    uart_write(stage);
    uart_write("\n");
    halt();
}

// The four checks that run before the console is discovered cannot print a
// stage, so the QEMU trace address is the only evidence. One shared halt()
// destroys that evidence: every early failure folds onto a single address and
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

// discover_hardware has five distinct ways to fail and, reported through one
// halt, they were indistinguishable - which is how two successive guesses at
// which one was firing both turned out to be wrong. The console is not up yet
// (finding it requires the inventory this call did not produce), so the halt
// address is the only channel available, and it carries one bit of
// information unless there is one address per cause.
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

[[nodiscard]] bool map_range(
    os::kernel::aarch64::EarlyStage1Builder& builder,
    std::uint64_t virtual_begin,
    std::uint64_t physical_begin,
    std::uint64_t length,
    MachinePermissions permissions,
    MachineMemoryKind kind) noexcept {
    if (length == 0ULL || (length % page_size) != 0ULL) return false;
    const std::uint64_t pages = length / page_size;
    for (std::uint64_t page = 0ULL; page < pages; ++page) {
        auto mapped = builder.map_page(
            virtual_begin + page * page_size,
            physical_begin + page * page_size,
            permissions,
            kind);
        if (!mapped) return false;
    }
    return true;
}

[[nodiscard]] bool map_identity_symbols(
    os::kernel::aarch64::EarlyStage1Builder& builder,
    const char* begin,
    const char* end,
    MachinePermissions permissions) noexcept {
    const auto start = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(begin));
    const auto finish = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(end));
    if (finish < start || (start & (page_size - 1ULL)) != 0ULL ||
        (finish & (page_size - 1ULL)) != 0ULL) return false;
    if (finish == start) return true;
    return map_range(
        builder, start, start, finish - start,
        permissions, MachineMemoryKind::normal);
}

[[noreturn]] void guarded_runtime_main() noexcept {
    uart_write("COOKIE:M7.5d:GUARDED\n");
    for (;;) asm volatile("wfe" ::: "memory");
}

} // namespace

extern "C" [[noreturn]] void cookie_aarch64_unhandled_exception() noexcept {
    uart_write("COOKIE:PANIC:EXCEPTION\n");
    halt();
}

extern "C" void cookie_kernel_syscall_entry(
    os::kernel::aarch64::ExceptionFrame*) noexcept {
    // M7.5d has not admitted an EL0 process yet. A lower-EL syscall at this
    // stage therefore indicates unexpected execution state and must not be
    // treated as a successful no-op.
    uart_write("COOKIE:PANIC:EARLY_SVC\n");
    halt();
}

extern "C" [[noreturn]] void cookie_aarch64_boot_main(std::uintptr_t dtb_physical) noexcept {
    // Vectors first, before any code that can fault. VBAR_EL1 has no
    // architectural reset value, so until this runs a synchronous exception
    // vectors to whatever base the implementation happens to start with - on
    // this board zero, where 0x200 is flash rather than a handler. The machine
    // then executes flash contents and the failure presents as a hang with no
    // handler having run and no halt having been reached.
    //
    // Installing them here costs one system register write and makes every
    // fault below land somewhere that identifies itself.
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
    const auto* uart = find_pl011(inventory.value());
    if (uart == nullptr || !uart->registers.valid()) halt_no_console();
    boot_uart = reinterpret_cast<volatile std::uint32_t*>(
        static_cast<std::uintptr_t>(uart->registers.base));

    // Checked after the console rather than beside the discovery call. A walk
    // that succeeds but yields no usable RAM is a different failure from a
    // walk that could not complete, and it is the one that can be reported:
    // finding the UART proves the device tree was traversed and that node
    // discovery works, which narrows the fault to how memory nodes in
    // particular are recognised.
    if (inventory.value().memory_count == 0U) fail("NO_MEMORY_NODE");

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

    os::kernel::aarch64::EarlyPageArena arena{
        plan.value().page_tables.base,
        plan.value().page_tables.end_exclusive()};
    if (!arena.valid()) fail("PAGE_ARENA");
    os::kernel::aarch64::EarlyStage1Builder builder{arena};
    auto root = builder.initialize();
    if (!root) fail("TABLE_ROOT");

    if (!map_identity_symbols(
            builder, __cookie_text_start, __cookie_text_end,
            MachinePermissions::read_execute)) fail("MAP_TEXT");
    if (!map_identity_symbols(
            builder, __cookie_rodata_start, __cookie_rodata_end,
            MachinePermissions::read)) fail("MAP_RODATA");
    if (!map_identity_symbols(
            builder, __cookie_data_start, __bss_end,
            MachinePermissions::read_write)) fail("MAP_DATA");
    if (!map_range(
            builder,
            plan.value().page_tables.base,
            plan.value().page_tables.base,
            plan.value().page_tables.size,
            MachinePermissions::read_write,
            MachineMemoryKind::normal)) fail("MAP_TABLES");
    if (!map_range(
            builder,
            dtb_range.base,
            dtb_range.base,
            dtb_range.size,
            MachinePermissions::read,
            MachineMemoryKind::normal)) fail("MAP_DTB");

    const auto uart_begin = align_down(uart->registers.base);
    std::uint64_t uart_end = 0ULL;
    if (uart->registers.base > UINT64_MAX - uart->registers.size ||
        !align_up(uart->registers.base + uart->registers.size, uart_end) ||
        !map_range(
            builder, uart_begin, uart_begin, uart_end - uart_begin,
            MachinePermissions::read_write, MachineMemoryKind::device)) fail("MAP_UART");

    os::kernel::MachinePhysicalLedger physical_ledger{};
    os::kernel::MachineAddressSpace kernel_space{};
    if (!os::kernel::machine_bind_address_space(kernel_space, physical_ledger)) fail("BIND_SPACE");
    if (!os::kernel::aarch64_attach_early_stage1(kernel_space, builder)) fail("ATTACH_STAGE1");
    if (!os::kernel::machine_map_kernel_stack(
            kernel_space,
            static_cast<std::uintptr_t>(runtime_stack_virtual),
            static_cast<std::uintptr_t>(plan.value().kernel_stack.base),
            static_cast<std::size_t>(plan.value().kernel_stack.size))) fail("MAP_STACK");

    // Vectors were installed at entry and stay installed across the MMU
    // transition: VBAR_EL1 holds a virtual address that the identity mapping
    // above keeps valid, so enabling translation does not invalidate them.
    // This matters most for the activation itself, which is the single most
    // likely instruction here to fault.
    if (!os::kernel::aarch64::activate_stage1_translation(root.value())) fail("ACTIVATE_MMU");
    uart_write("COOKIE:M7.5d:MMU\n");

    os::kernel::MachineContext bootstrap_context{};
    os::kernel::MachineContext runtime_context{};
    const auto stack_top = runtime_stack_virtual + plan.value().kernel_stack.size;
    if (!os::kernel::machine_prepare_context(
            runtime_context,
            kernel_space,
            reinterpret_cast<std::uintptr_t>(&guarded_runtime_main),
            static_cast<std::uintptr_t>(stack_top))) fail("PREPARE_CONTEXT");

    os::kernel::machine_switch_context(bootstrap_context, runtime_context);
    halt();
}
