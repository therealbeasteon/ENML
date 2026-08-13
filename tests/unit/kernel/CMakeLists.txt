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

add_executable(kernel_composition_test kernel_test.cpp)
target_link_libraries(kernel_composition_test PRIVATE emnl::oskernel emnl_warnings)
target_compile_options(kernel_composition_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME kernel_composition_test COMMAND kernel_composition_test)
set_tests_properties(kernel_composition_test PROPERTIES LABELS "m7-kernel")
