#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <os/core/span.hpp>
#include <os/kernel/abi.hpp>
#include <os/kernel/aarch64_entry.hpp>
#include <os/kernel/aarch64_page_tables.hpp>
#include <os/kernel/aarch64_translation.hpp>
#include <os/kernel/boot_memory.hpp>
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
constexpr std::uint64_t user_code_virtual = 0x0000'0000'1000'0000ULL;
constexpr std::uint64_t user_stack_virtual = 0x0000'0000'1001'0000ULL;
constexpr std::uint64_t syscall_return_cookie = 0xC00CULL;

volatile std::uint32_t* boot_uart = nullptr;
std::uint32_t el0_yield_count = 0U;

// These are persistent kernel authorities, not temporary bootstrap locals.
// Keeping them in BSS avoids consuming most of the 16 KiB bootstrap stack with
// the 256-entry physical ledger and per-address-space mapping table.
os::kernel::MachinePhysicalLedger boot_physical_ledger{};
os::kernel::MachineAddressSpace boot_kernel_space{};

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
    if (!valid) {
        uart_write("COOKIE:PANIC:EL0_CONTEXT\n");
        halt();
    }
    cookie_aarch64_enter_el0(user_code_virtual, user_stack_top);
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

extern "C" [[noreturn]] void cookie_aarch64_boot_main(std::uintptr_t dtb_physical) noexcept {
    const auto dtb_blob = bounded_dtb(dtb_physical);
    if (dtb_blob.empty()) halt();

    auto fdt = os::kernel::FdtView::parse(dtb_blob);
    if (!fdt) halt();
    auto inventory = os::kernel::discover_hardware(fdt.value());
    if (!inventory || inventory.value().memory_count == 0U) halt();

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
            static_cast<std::size_t>(page_size))) halt();

    const auto* uart = find_pl011(inventory.value());
    if (uart == nullptr || !uart->registers.valid()) halt();
    const auto uart_begin = align_down(uart->registers.base);
    std::uint64_t uart_end = 0ULL;
    if (uart->registers.base > UINT64_MAX - uart->registers.size ||
        !align_up(uart->registers.base + uart->registers.size, uart_end) ||
        !map_machine_range(
            boot_kernel_space, uart_begin, uart_begin, uart_end - uart_begin,
            MachinePermissions::read_write, MachineMemoryKind::device)) halt();
    boot_uart = reinterpret_cast<volatile std::uint32_t*>(
        static_cast<std::uintptr_t>(uart->registers.base));

    if (!os::kernel::aarch64::activate_stage1_translation(root.value())) halt();
    if (!os::kernel::aarch64::install_exception_vectors()) halt();
    uart_write("COOKIE:M7.5d:MMU\n");

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
