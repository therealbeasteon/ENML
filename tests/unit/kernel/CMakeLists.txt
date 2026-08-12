add_executable(kernel_abi_test abi_test.cpp)
target_link_libraries(kernel_abi_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_abi_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_abi_test COMMAND kernel_abi_test)
set_tests_properties(kernel_abi_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_rendezvous_test rendezvous_test.cpp)
target_link_libraries(kernel_rendezvous_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_rendezvous_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_rendezvous_test COMMAND kernel_rendezvous_test)
set_tests_properties(kernel_rendezvous_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_capability_test capability_test.cpp)
target_link_libraries(kernel_capability_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_capability_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_capability_test COMMAND kernel_capability_test)
set_tests_properties(kernel_capability_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_interrupt_test interrupt_test.cpp)
target_link_libraries(kernel_interrupt_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_interrupt_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_interrupt_test COMMAND kernel_interrupt_test)
set_tests_properties(kernel_interrupt_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_scheduler_test scheduler_test.cpp)
target_link_libraries(kernel_scheduler_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_scheduler_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_scheduler_test COMMAND kernel_scheduler_test)
set_tests_properties(kernel_scheduler_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_scheduler_deadline_test scheduler_deadline_test.cpp)
target_link_libraries(kernel_scheduler_deadline_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_scheduler_deadline_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_scheduler_deadline_test COMMAND kernel_scheduler_deadline_test)
set_tests_properties(kernel_scheduler_deadline_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_machine_host_test machine_host_test.cpp)
target_link_libraries(kernel_machine_host_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_machine_host_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_machine_host_test COMMAND kernel_machine_host_test)
set_tests_properties(kernel_machine_host_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_aarch64_contract_test aarch64_contract_test.cpp)
target_link_libraries(kernel_aarch64_contract_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_aarch64_contract_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_aarch64_contract_test COMMAND kernel_aarch64_contract_test)
set_tests_properties(kernel_aarch64_contract_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_aarch64_exception_test aarch64_exception_test.cpp)
target_link_libraries(kernel_aarch64_exception_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_aarch64_exception_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_aarch64_exception_test COMMAND kernel_aarch64_exception_test)
set_tests_properties(kernel_aarch64_exception_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_aarch64_user_frames_test aarch64_user_frames_test.cpp)
target_link_libraries(kernel_aarch64_user_frames_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_aarch64_user_frames_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_aarch64_user_frames_test COMMAND kernel_aarch64_user_frames_test)
set_tests_properties(kernel_aarch64_user_frames_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_aarch64_translation_test aarch64_translation_test.cpp)
target_link_libraries(kernel_aarch64_translation_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_aarch64_translation_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_aarch64_translation_test COMMAND kernel_aarch64_translation_test)
set_tests_properties(kernel_aarch64_translation_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_aarch64_page_tables_test aarch64_page_tables_test.cpp)
target_link_libraries(kernel_aarch64_page_tables_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_aarch64_page_tables_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_aarch64_page_tables_test COMMAND kernel_aarch64_page_tables_test)
set_tests_properties(kernel_aarch64_page_tables_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_aarch64_mapping_state_test aarch64_mapping_state_test.cpp)
target_link_libraries(kernel_aarch64_mapping_state_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_aarch64_mapping_state_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_aarch64_mapping_state_test COMMAND kernel_aarch64_mapping_state_test)
set_tests_properties(kernel_aarch64_mapping_state_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_fdt_test fdt_test.cpp)
target_link_libraries(kernel_fdt_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_fdt_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_fdt_test COMMAND kernel_fdt_test)
set_tests_properties(kernel_fdt_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_hardware_inventory_test hardware_inventory_test.cpp)
target_link_libraries(kernel_hardware_inventory_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_hardware_inventory_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_hardware_inventory_test COMMAND kernel_hardware_inventory_test)
set_tests_properties(kernel_hardware_inventory_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_gic_v3_discovery_test gic_v3_discovery_test.cpp)
target_link_libraries(kernel_gic_v3_discovery_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_gic_v3_discovery_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_gic_v3_discovery_test COMMAND kernel_gic_v3_discovery_test)
set_tests_properties(kernel_gic_v3_discovery_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_arch_timer_discovery_test arch_timer_discovery_test.cpp)
target_link_libraries(kernel_arch_timer_discovery_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_arch_timer_discovery_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_arch_timer_discovery_test COMMAND kernel_arch_timer_discovery_test)
set_tests_properties(kernel_arch_timer_discovery_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_boot_memory_test boot_memory_test.cpp)
target_link_libraries(kernel_boot_memory_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_boot_memory_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_boot_memory_test COMMAND kernel_boot_memory_test)
set_tests_properties(kernel_boot_memory_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_boot_memory_plan_test boot_memory_plan_test.cpp)
target_link_libraries(kernel_boot_memory_plan_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_boot_memory_plan_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_boot_memory_plan_test COMMAND kernel_boot_memory_plan_test)
set_tests_properties(kernel_boot_memory_plan_test PROPERTIES LABELS "m7-kernel")

add_executable(kernel_composition_test kernel_test.cpp)
target_link_libraries(kernel_composition_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_composition_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_composition_test COMMAND kernel_composition_test)
set_tests_properties(kernel_composition_test PROPERTIES LABELS "m7-kernel")
