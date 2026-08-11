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
