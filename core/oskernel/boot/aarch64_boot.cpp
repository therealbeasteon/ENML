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
#include <os/kernel/aarch64_kernel_mapping_manifest.hpp>
#include <os/kernel/aarch64_page_tables.hpp>
#include <os/kernel/aarch64_preemption.hpp>
#include <os/kernel/aarch64_translation.hpp>
#include <os/kernel/aarch64_translation_root_sealer.hpp>
#include <os/kernel/address_space_epoch.hpp>
#include <os/kernel/arch_timer_discovery.hpp>
#include <os/kernel/boot_memory.hpp>
#include <os/kernel/boot_memory_plan.hpp>
#include <os/kernel/fdt.hpp>
#include <os/kernel/gic_v3_discovery.hpp>
#include <os/kernel/hardware_inventory.hpp>
#include <os/kernel/machine.hpp>
#include <os/kernel/machine_aarch64.hpp>
#include <os/kernel/process_translation.hpp>
#include <os/kernel/scheduler.hpp>

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
constexpr std::uint64_t syscall_return_cookie = 0xC00CULL;
constexpr std::uint64_t process_a_marker = 0xA11AULL;
constexpr std::uint64_t process_b_marker = 0xB22BULL;
constexpr os::kernel::ThreadId process_a_thread = 1U;
constexpr os::kernel::ThreadId process_b_thread = 2U;
constexpr os::kernel::Priority process_priority = 4U;

volatile std::uint32_t* boot_uart = nullptr;
std::uint32_t el0_yield_count = 0U;
std::uint32_t timer_irq_count = 0U;
os::kernel::aarch64::GicV3PrimaryCpu boot_gic{};

// Long-lived authority belongs in BSS, not on the bootstrap stack.
os::kernel::MachinePhysicalLedger boot_physical_ledger{};
os::kernel::MachineAddressSpace boot_kernel_space{};
os::kernel::MachineAddressSpace process_space_a{};
os::kernel::MachineAddressSpace process_space_b{};
os::kernel::AddressSpaceEpochAuthority boot_epochs{};
os::kernel::ProcessTranslationTable boot_translations{};
os::kernel::Scheduler boot_scheduler{};
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
}

void install_process_b_program(std::uint64_t physical_page) noexcept {
    zero_page(physical_page);
    auto* words = reinterpret_cast<volatile std::uint32_t*>(
        static_cast<std::uintptr_t>(physical_page));
    constexpr std::uint32_t movz_x19_base = 0xD2800013U;
    constexpr std::uint32_t branch_self = 0x14000000U;
    words[0] = movz_x19_base | (static_cast<std::uint32_t>(process_b_marker) << 5U);
    words[1] = branch_self;
}

[[nodiscard]] bool commit_result(
    const os::kernel::aarch64::PreemptionResult& result,
    std::uint64_t now_nanoseconds) noexcept {
    auto plan = os::kernel::aarch64::prepare_execution_universe(result);
    if (!plan) return false;
    return static_cast<bool>(
        os::kernel::aarch64::commit_execution_universe(plan.value(), now_nanoseconds));
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
    auto start = boot_preemption.start(
        boot_scheduler, boot_translations, boot_epochs, now, live);
    if (!start || start.value().next != process_a_thread ||
        start.value().switched == false || start.value().deadline.active ||
        !commit_result(start.value(), now)) {
        uart_write("COOKIE:PANIC:EL0_START\n");
        halt();
    }

    cookie_aarch64_enter_el0(live.elr_el1, live.sp_el0);
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
        call.value().argument_count != 0U ||
        boot_preemption.running() != process_a_thread) {
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
        if (!boot_scheduler.update(process_b_thread, true, process_priority)) {
            uart_write("COOKIE:PANIC:SCHED_WAKE\n");
            halt();
        }
        const auto now = os::kernel::machine_monotonic_nanoseconds();
        auto rescheduled = boot_preemption.reschedule(
            boot_scheduler, boot_translations, boot_epochs, now, *frame);
        if (!rescheduled || rescheduled.value().next != process_a_thread ||
            rescheduled.value().switched || !rescheduled.value().deadline.active ||
            !commit_result(rescheduled.value(), now)) {
            uart_write("COOKIE:PANIC:SCHED_EVENT\n");
            halt();
        }
        uart_write("COOKIE:M7.5i:CONTENTION_ARMED\n");
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

    const auto running = boot_preemption.running();
    if ((running == process_a_thread && frame->x[19] != process_a_marker) ||
        (running == process_b_thread && frame->x[19] != process_b_marker) ||
        (running != process_a_thread && running != process_b_thread)) {
        uart_write("COOKIE:PANIC:UNIVERSE_MARKER\n");
        halt();
    }

    ++timer_irq_count;
    const auto delivered = boot_preemption.current_deadline();
    const auto now = os::kernel::machine_monotonic_nanoseconds();
    auto next = boot_preemption.on_timer(
        boot_scheduler, boot_translations, boot_epochs, delivered, now, *frame);
    if (!next || !commit_result(next.value(), now)) {
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
        if (running != process_a_thread || next.value().next != process_b_thread) halt();
        uart_write("COOKIE:M7.5i:ISOLATED_A_B_A\n");
    }
}

extern "C" [[noreturn]] void cookie_aarch64_boot_main(std::uintptr_t dtb_physical) noexcept {
    const auto dtb_blob = bounded_dtb(dtb_physical);
    if (dtb_blob.empty()) halt();

    auto fdt = os::kernel::FdtView::parse(dtb_blob);
    if (!fdt) halt();
    auto inventory = os::kernel::discover_hardware(fdt.value());
    auto gic_topology = os::kernel::discover_gic_v3(fdt.value());
    auto timer = os::kernel::discover_architected_timer(fdt.value());
    if (!inventory || inventory.value().memory_count == 0U ||
        !gic_topology || !timer ||
        (timer.value().trigger_flags & 0xFU) != 4U) halt();

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

    const std::array<HardwareRange, 4U> a_code_protected{
        image_range, dtb_range, plan.value().page_tables, plan.value().kernel_stack};
    auto a_code = os::kernel::select_early_ram(
        inventory.value(), page_size, page_size,
        std::span<const HardwareRange>{a_code_protected});
    if (!a_code) halt();

    const std::array<HardwareRange, 5U> a_stack_protected{
        image_range, dtb_range, plan.value().page_tables, plan.value().kernel_stack,
        a_code.value()};
    auto a_stack = os::kernel::select_early_ram(
        inventory.value(), page_size, page_size,
        std::span<const HardwareRange>{a_stack_protected});
    if (!a_stack) halt();

    const std::array<HardwareRange, 6U> b_code_protected{
        image_range, dtb_range, plan.value().page_tables, plan.value().kernel_stack,
        a_code.value(), a_stack.value()};
    auto b_code = os::kernel::select_early_ram(
        inventory.value(), page_size, page_size,
        std::span<const HardwareRange>{b_code_protected});
    if (!b_code) halt();

    const std::array<HardwareRange, 7U> b_stack_protected{
        image_range, dtb_range, plan.value().page_tables, plan.value().kernel_stack,
        a_code.value(), a_stack.value(), b_code.value()};
    auto b_stack = os::kernel::select_early_ram(
        inventory.value(), page_size, page_size,
        std::span<const HardwareRange>{b_stack_protected});
    if (!b_stack) halt();

    install_process_a_program(a_code.value().base);
    install_process_b_program(b_code.value().base);
    zero_page(a_stack.value().base);
    zero_page(b_stack.value().base);

    os::kernel::aarch64::EarlyPageArena arena{
        plan.value().page_tables.base,
        plan.value().page_tables.end_exclusive()};
    if (!arena.valid()) halt();
    os::kernel::aarch64::EarlyStage1Builder boot_builder{arena};
    os::kernel::aarch64::EarlyStage1Builder builder_a{arena};
    os::kernel::aarch64::EarlyStage1Builder builder_b{arena};
    auto boot_root = boot_builder.initialize();
    if (!boot_root || !builder_a.initialize() || !builder_b.initialize()) halt();

    if (!os::kernel::machine_bind_address_space(boot_kernel_space, boot_physical_ledger) ||
        !os::kernel::machine_bind_address_space(process_space_a, boot_physical_ledger) ||
        !os::kernel::machine_bind_address_space(process_space_b, boot_physical_ledger) ||
        !os::kernel::aarch64_attach_early_stage1(boot_kernel_space, boot_builder) ||
        !os::kernel::aarch64_attach_early_stage1(process_space_a, builder_a) ||
        !os::kernel::aarch64_attach_early_stage1(process_space_b, builder_b)) halt();

    const auto* uart = find_pl011(inventory.value());
    if (uart == nullptr || !uart->registers.valid()) halt();

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
        !add_device_manifest(kernel_manifest, uart->registers)) halt();

    for (std::size_t i = 0U; i < gic_topology.value().redistributor_count; ++i) {
        if (!add_device_manifest(kernel_manifest, gic_topology.value().redistributors[i])) halt();
    }

    if (!os::kernel::aarch64::replay_kernel_mapping_manifest(kernel_manifest, boot_kernel_space) ||
        !os::kernel::aarch64::replay_kernel_mapping_manifest(kernel_manifest, process_space_a) ||
        !os::kernel::aarch64::replay_kernel_mapping_manifest(kernel_manifest, process_space_b) ||
        !os::kernel::machine_map(
            boot_kernel_space,
            static_cast<std::uintptr_t>(plan.value().page_tables.base),
            static_cast<std::uintptr_t>(plan.value().page_tables.base),
            static_cast<std::size_t>(plan.value().page_tables.size),
            MachinePermissions::read_write,
            MachineMemoryKind::normal) ||
        !os::kernel::machine_map(
            boot_kernel_space,
            static_cast<std::uintptr_t>(dtb_range.base),
            static_cast<std::uintptr_t>(dtb_range.base),
            static_cast<std::size_t>(dtb_range.size),
            MachinePermissions::read,
            MachineMemoryKind::normal) ||
        !os::kernel::aarch64_map_user(
            process_space_a,
            static_cast<std::uintptr_t>(user_code_virtual),
            static_cast<std::uintptr_t>(a_code.value().base),
            static_cast<std::size_t>(page_size),
            MachinePermissions::read_execute) ||
        !os::kernel::aarch64_map_user_stack(
            process_space_a,
            static_cast<std::uintptr_t>(user_stack_virtual),
            static_cast<std::uintptr_t>(a_stack.value().base),
            static_cast<std::size_t>(page_size)) ||
        !os::kernel::aarch64_map_user(
            process_space_b,
            static_cast<std::uintptr_t>(user_code_virtual),
            static_cast<std::uintptr_t>(b_code.value().base),
            static_cast<std::size_t>(page_size),
            MachinePermissions::read_execute) ||
        !os::kernel::aarch64_map_user_stack(
            process_space_b,
            static_cast<std::uintptr_t>(user_stack_virtual),
            static_cast<std::uintptr_t>(b_stack.value().base),
            static_cast<std::size_t>(page_size))) halt();

    auto sealed_a = os::kernel::aarch64::TranslationRootSealer::seal(builder_a);
    auto sealed_b = os::kernel::aarch64::TranslationRootSealer::seal(builder_b);
    auto epoch_a = boot_epochs.acquire();
    auto epoch_b = boot_epochs.acquire();
    if (!sealed_a || !sealed_b || !epoch_a || !epoch_b ||
        !boot_translations.bind(process_a_thread, epoch_a.value(), sealed_a.value(), boot_epochs) ||
        !boot_translations.bind(process_b_thread, epoch_b.value(), sealed_b.value(), boot_epochs) ||
        !boot_scheduler.admit(process_a_thread, process_priority) ||
        !boot_scheduler.admit(process_b_thread, process_priority) ||
        !boot_scheduler.update(process_b_thread, false, process_priority)) halt();

    os::kernel::aarch64::ExceptionFrame initial_a{};
    initial_a.elr_el1 = user_code_virtual;
    initial_a.sp_el0 = user_stack_virtual + page_size;
    initial_a.spsr_el1 = 0x340ULL;
    os::kernel::aarch64::ExceptionFrame initial_b = initial_a;
    if (!boot_preemption.admit_frame(process_a_thread, initial_a) ||
        !boot_preemption.admit_frame(process_b_thread, initial_b)) halt();

    boot_uart = reinterpret_cast<volatile std::uint32_t*>(
        static_cast<std::uintptr_t>(uart->registers.base));

    if (!os::kernel::aarch64::activate_stage1_translation(boot_root.value()) ||
        !os::kernel::aarch64::install_exception_vectors()) halt();
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
