#include <os/kernel/aarch64_translation.hpp>

#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
}

int main() {
    using namespace os::kernel;
    using namespace os::kernel::aarch64;

    require(stage1_t0sz == 25U);
    require(stage1_virtual_address((1ULL << 39U) - 1ULL));
    require(!stage1_virtual_address(1ULL << 39U));
    require(page_aligned(0x4000ULL));
    require(!page_aligned(0x4001ULL));

    require(level1_index(0x0000'0001'8000'0000ULL) == 6U);
    require(level2_index(0x0000'0001'8020'0000ULL) == 1U);
    require(level3_index(0x0000'0001'8020'3000ULL) == 3U);

    const auto table = table_descriptor(0x0000'0000'0040'0000ULL);
    require((table & descriptor::valid) != 0ULL);
    require((table & descriptor::table_or_page) != 0ULL);
    require(table_descriptor(0x4001ULL) == 0ULL);

    const auto rw = page_descriptor(
        0x0000'0000'0080'0000ULL,
        MachinePermissions::read_write,
        MachineMemoryKind::normal);
    require(rw != 0ULL);
    require((rw & descriptor::privileged_execute_never) != 0ULL);
    require((rw & descriptor::unprivileged_execute_never) != 0ULL);
    require((rw & descriptor::access_flag) != 0ULL);

    const auto rx = page_descriptor(
        0x0000'0000'0080'1000ULL,
        MachinePermissions::read_execute,
        MachineMemoryKind::normal);
    require(rx != 0ULL);
    require((rx & descriptor::ap_read_only_el1) != 0ULL);
    require((rx & descriptor::privileged_execute_never) == 0ULL);
    require((rx & descriptor::unprivileged_execute_never) != 0ULL);

    const auto dev = page_descriptor(
        0x0000'0000'0900'0000ULL,
        MachinePermissions::read_write,
        MachineMemoryKind::device);
    require(dev != 0ULL);
    require((dev & descriptor::privileged_execute_never) != 0ULL);
    require(page_descriptor(
        0x0000'0000'0900'0000ULL,
        MachinePermissions::read_execute,
        MachineMemoryKind::device) == 0ULL);

    require(cookie_ips(0U) == 0U);
    require(cookie_ips(5U) == 5U);
    require(cookie_ips(6U) == 5U);
    require(tcr_el1_for_ips(5U) != 0ULL);
    require(tcr_el1_for_ips(6U) == 0ULL);

    return 0;
}
