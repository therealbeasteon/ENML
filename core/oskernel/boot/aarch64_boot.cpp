#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <os/core/span.hpp>
#include <os/kernel/abi.hpp>
#include <os/kernel/aarch64_entry.hpp>
#include <os/kernel/aarch64_gic_v3.hpp>
#include <os/kernel/aarch64_page_tables.hpp>
#include <os/kernel/aarch64_translation.hpp>
#include <os/kernel/arch_timer_discovery.hpp>
#include <os/kernel/boot_memory.hpp>
#include <os/kernel/boot_memory_plan.hpp>
#include <os/kernel/fdt.hpp>
#include <os/kernel/gic_v3_discovery.hpp>
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
constexpr std::uint64_t user_code_virtual = 0x0000'0000'1000'0000ULL;
constexpr std::uint64_t user_stack_virtual = 0x0000'0000'1001'0000ULL;
constexpr std::uint64_t syscall_return_cookie = 0xC00CULL;
constexpr std::uint64_t timer_period_ns = 2'000'000ULL;

volatile std::uint32_t* boot_uart = nullptr;
std::uint32_t el0_yield_count = 0U;
std::uint32_t timer_irq_count = 0U;
os::kernel::aarch64::GicV3PrimaryCpu boot_gic{};

// Persistent machine authority belongs in BSS, not on the 16 KiB bootstrap stack.
os::kernel::MachinePhysicalLedger boot_physical_ledger{};
os::kernel::MachineAddressSpace boot_kernel_space{};

// TEMPORARY diagnostic: QEMU virt's PL011 is always at this fixed physical
// address, and translation is off for the whole pre-MMU window, so this is
// safe to use before boot_uart is discovered/mapped. Reverted once the M7.5g
// silent-hang regression is found - see the CI run where cookie-qemu.log came
// back completely empty despite the image linking and QEMU launching.
void debug_checkpoint(char tag) noexcept {
    auto* const raw = reinterpret_cast<volatile std::uint32_t*>(0x09000000ULL);
    raw[0] = static_cast<std::uint32_t>(static_cast<unsigned char>(tag));
}

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

[[nodiscard]] const os::kernel::DiscoveredDevice*
find_pl011(const os::kernel::HardwareInventory& inventory) noexcept {
    for (std::size_t i = 0U; i < inventory.device_count; ++i) {
        if (inventory.devices[i].compatible_view() == "arm,pl011") return &inventory.devices[i];
    }
    return nullptr;
}

[[nodiscard]] bool map_machine_range(
    os::kernel::MachineAddressSpace& space,
    std::uint64_t virtual_begin,
    std::uint64_t physical_begin,
    std::uint64_t length,
    MachinePermissions permissions,
    MachineMemoryKind kind) noexcept {
    if (length == 0ULL || length > static_cast<std::uint64_t>(SIZE_MAX)) return false;
    auto mapped = os::kernel::machine_map(
        space,
        static_cast<std::uintptr_t>(virtual_begin),
        static_cast<std::uintptr_t>(physical_begin),
        static_cast<std::size_t>(length),
        permissions,
        kind);
    return static_cast<bool>(mapped);
}

[[nodiscard]] bool map_identity_symbols(
    os::kernel::MachineAddressSpace& space,
    const char* begin,
    const char* end,
    MachinePermissions permissions) noexcept {
    const auto start = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(begin));
    const auto finish = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(end));
    if (finish < start || (start & (page_size - 1ULL)) != 0ULL ||
        (finish & (page_size - 1ULL)) != 0ULL) return false;
    if (finish == start) return true;
    return map_machine_range(
        space, start, start, finish - start,
        permissions, MachineMemoryKind::normal);
}

[[nodiscard]] bool map_device_range(os::kernel::MachineAddressSpace& space, HardwareRange range) noexcept {
    if (!range.valid()) return false;
    const auto begin = align_down(range.base);
    std::uint64_t end = 0ULL;
    if (range.base > UINT64_MAX - range.size ||
        !align_up(range.base + range.size, end) || end <= begin) return false;
    return map_machine_range(
        space, begin, begin, end - begin,
        MachinePermissions::read_write, MachineMemoryKind::device);
}

void install_first_user_program(std::uint64_t physical_page) noexcept {
    auto* words = reinterpret_cast<volatile std::uint32_t*>(
        static_cast<std::uintptr_t>(physical_page));
    constexpr std::uint32_t movz_x8_base = 0xD2800008U;
    constexpr std::uint32_t svc_zero = 0xD4000001U;
    constexpr std::uint32_t branch_self = 0x14000000U;
    constexpr std::uint32_t yield_number =
        static_cast<std::uint32_t>(os::kernel::KernelCall::yield);
    words[0] = movz_x8_base | (yield_number << 5U);
    words[1] = svc_zero;
    words[2] = movz_x8_base | (yield_number << 5U);
    words[3] = svc_zero;
    words[4] = branch_self;
}

[[noreturn]] void guarded_runtime_main() noexcept {
    uart_write("COOKIE:M7.5d:GUARDED\n");
    const auto user_stack_top = user_stack_virtual + page_size;
    auto valid = os::kernel::aarch64_validate_user_context(
        boot_kernel_space,
        static_cast<std::uintptr_t>(user_code_virtual),
        static_cast<std::uintptr_t>(user_stack_top));
    if (!valid || !boot_gic.initialized || !os::kernel::machine_set_timer(timer_period_ns)) {
        uart_write("COOKIE:PANIC:EL0_CONTEXT\n");
        halt();
    }
    os::kernel::cookie_aarch64_enter_el0(user_code_virtual, user_stack_top);
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
    if (!call || call.value().call != os::kernel::KernelCall::yield ||
        call.value().authority != os::kernel::CallAuthority::unprivileged ||
        call.value().argument_count != 0U) {
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

    uart_write("COOKIE:PANIC:EL0_STATE\n");
    halt();
}

extern "C" void cookie_aarch64_irq_dispatch(
    os::kernel::aarch64::ExceptionFrame* frame) noexcept {
    if (frame == nullptr || !boot_gic.initialized) halt();
    const auto intid = os::kernel::aarch64::gic_v3_acknowledge();
    if (intid >= 1020U && intid <= 1023U) return;
    if (intid != boot_gic.timer_intid) {
        uart_write("COOKIE:PANIC:UNEXPECTED_IRQ\n");
        halt();
    }

    ++timer_irq_count;
    // Move the compare value into the future before EOI so the level-triggered
    // timer PPI is deasserted when the GIC reevaluates it.
    if (!os::kernel::machine_set_timer(timer_period_ns)) {
        uart_write("COOKIE:PANIC:TIMER_REARM\n");
        halt();
    }
    os::kernel::aarch64::gic_v3_end_interrupt(intid);

    if (timer_irq_count == 1U) {
        uart_write("COOKIE:M7.5g:TIMER_IRQ\n");
    } else if (timer_irq_count == 2U) {
        // A second interrupt proves the first handler returned through ERET and
        // IRQ delivery resumed in EL0 rather than leaving execution stranded at EL1.
        uart_write("COOKIE:M7.5g:TIMER_IRQ_RETURN\n");
    }
}

extern "C" [[noreturn]] void cookie_aarch64_boot_main(std::uintptr_t dtb_physical) noexcept {
    // Vectors first, before any code that can fault. VBAR_EL1 has no
    // architectural reset value, so until this runs a synchronous exception
    // vectors to whatever base the implementation happens to start with - on
    // this board zero, where 0x200 is flash rather than a handler. The machine
    // then executes flash and the failure presents as a hang with no handler
    // having run and no halt having been reached, which is exactly how the
    // M7.5d boot fault hid for as long as it did.
    //
    // Installing them here costs one system register write. They stay valid
    // across the MMU transition because the identity mapping below keeps the
    // address they point at.
    if (!os::kernel::aarch64::install_exception_vectors()) halt();
    debug_checkpoint('1');

    const auto dtb_blob = bounded_dtb(dtb_physical);
    if (dtb_blob.empty()) halt();
    debug_checkpoint('2');

    auto fdt = os::kernel::FdtView::parse(dtb_blob);
    if (!fdt) halt();
    debug_checkpoint('3');
    auto inventory = os::kernel::discover_hardware(fdt.value());
    debug_checkpoint('4');
    auto gic_topology = os::kernel::discover_gic_v3(fdt.value());
    debug_checkpoint('5');
    auto timer = os::kernel::discover_architected_timer(fdt.value());
    debug_checkpoint('6');
    if (!inventory) debug_checkpoint('a');
    if (inventory && inventory.value().memory_count == 0U) debug_checkpoint('b');
    if (!gic_topology) {
        debug_checkpoint('c');
        const auto code = gic_topology.error().code;
        if (code >= 90U && code <= 99U) {
            debug_checkpoint(static_cast<char>('0' + (code - 90U)));
        } else {
            debug_checkpoint('?');
        }
    }
    if (!timer) debug_checkpoint('d');
    if (timer && (timer.value().trigger_flags & 0xFU) != 4U) debug_checkpoint('e');
    if (!inventory || inventory.value().memory_count == 0U ||
        !gic_topology || !timer ||
        (timer.value().trigger_flags & 0xFU) != 4U) halt();
    debug_checkpoint('7');

    const auto image_begin = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(__cookie_image_start));
    const auto image_end = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(__cookie_image_end));
    std::uint64_t dtb_end = 0ULL;
    if (dtb_physical > UINT64_MAX - dtb_blob.size() ||
        !align_up(static_cast<std::uint64_t>(dtb_physical) + dtb_blob.size(), dtb_end)) halt();

    const HardwareRange image_range{align_down(image_begin), image_end - align_down(image_begin)};
    const HardwareRange dtb_range{
        align_down(static_cast<std::uint64_t>(dtb_physical)),
        dtb_end - align_down(static_cast<std::uint64_t>(dtb_physical))};
    if (!image_range.valid() || !dtb_range.valid()) halt();

    const std::array<HardwareRange, 2U> protected_ranges{image_range, dtb_range};
    auto plan = os::kernel::plan_early_boot_memory(
        inventory.value(), std::span<const HardwareRange>{protected_ranges},
        early_page_table_pages, runtime_stack_pages, page_size);
    if (!plan || !plan.value().valid()) halt();
    debug_checkpoint('8');

    const std::array<HardwareRange, 4U> user_code_protected{
        image_range, dtb_range, plan.value().page_tables, plan.value().kernel_stack};
    auto user_code = os::kernel::select_early_ram(
        inventory.value(), page_size, page_size,
        std::span<const HardwareRange>{user_code_protected});
    if (!user_code) halt();

    const std::array<HardwareRange, 5U> user_stack_protected{
        image_range, dtb_range, plan.value().page_tables,
        plan.value().kernel_stack, user_code.value()};
    auto user_stack = os::kernel::select_early_ram(
        inventory.value(), page_size, page_size,
        std::span<const HardwareRange>{user_stack_protected});
    if (!user_stack) halt();

    install_first_user_program(user_code.value().base);
    debug_checkpoint('9');

    os::kernel::aarch64::EarlyPageArena arena{
        plan.value().page_tables.base,
        plan.value().page_tables.end_exclusive()};
    if (!arena.valid()) halt();
    os::kernel::aarch64::EarlyStage1Builder builder{arena};
    auto root = builder.initialize();
    if (!root) halt();

    if (!os::kernel::machine_bind_address_space(
            boot_kernel_space, boot_physical_ledger) ||
        !os::kernel::aarch64_attach_early_stage1(boot_kernel_space, builder)) halt();
    debug_checkpoint('A');

    if (!map_identity_symbols(
            boot_kernel_space, __cookie_text_start, __cookie_text_end,
            MachinePermissions::read_execute) ||
        !map_identity_symbols(
            boot_kernel_space, __cookie_rodata_start, __cookie_rodata_end,
            MachinePermissions::read) ||
        !map_identity_symbols(
            boot_kernel_space, __cookie_data_start, __bss_end,
            MachinePermissions::read_write) ||
        !map_machine_range(
            boot_kernel_space,
            plan.value().page_tables.base,
            plan.value().page_tables.base,
            plan.value().page_tables.size,
            MachinePermissions::read_write,
            MachineMemoryKind::normal) ||
        !map_machine_range(
            boot_kernel_space,
            dtb_range.base,
            dtb_range.base,
            dtb_range.size,
            MachinePermissions::read,
            MachineMemoryKind::normal) ||
        !os::kernel::machine_map_kernel_stack(
            boot_kernel_space,
            static_cast<std::uintptr_t>(runtime_stack_virtual),
            static_cast<std::uintptr_t>(plan.value().kernel_stack.base),
            static_cast<std::size_t>(plan.value().kernel_stack.size)) ||
        !os::kernel::aarch64_map_user(
            boot_kernel_space,
            static_cast<std::uintptr_t>(user_code_virtual),
            static_cast<std::uintptr_t>(user_code.value().base),
            static_cast<std::size_t>(page_size),
            MachinePermissions::read_execute) ||
        !os::kernel::aarch64_map_user_stack(
            boot_kernel_space,
            static_cast<std::uintptr_t>(user_stack_virtual),
            static_cast<std::uintptr_t>(user_stack.value().base),
            static_cast<std::size_t>(page_size)) ||
        !map_device_range(boot_kernel_space, gic_topology.value().distributor)) halt();
    debug_checkpoint('B');

    for (std::size_t i = 0U; i < gic_topology.value().redistributor_count; ++i) {
        if (!map_device_range(boot_kernel_space, gic_topology.value().redistributors[i])) halt();
    }
    debug_checkpoint('C');

    const auto* uart = find_pl011(inventory.value());
    if (uart == nullptr || !uart->registers.valid() ||
        !map_device_range(boot_kernel_space, uart->registers)) halt();
    debug_checkpoint('D');
    boot_uart = reinterpret_cast<volatile std::uint32_t*>(
        static_cast<std::uintptr_t>(uart->registers.base));

    // Vectors were installed at entry and survive the transition: VBAR_EL1
    // holds an address the identity mapping above keeps valid. Reinstalling
    // here would be harmless but misleading - it would suggest the activation
    // on the line above ran without a handler, which is the arrangement that
    // made the M7.5d fault unreportable.
    if (!os::kernel::aarch64::activate_stage1_translation(root.value())) halt();
    uart_write("COOKIE:M7.5d:MMU\n");

    auto gic = os::kernel::aarch64::initialize_gic_v3_primary_cpu(
        gic_topology.value(), timer.value().nonsecure_physical_intid);
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
            static_cast<std::uintptr_t>(stack_top))) halt();

    os::kernel::machine_switch_context(bootstrap_context, runtime_context);
    halt();
}
