add_executable(abi_syscall_test syscall_abi_test.cpp)
# Both sides on purpose. The stub library is the thing under test and the kernel
# is the reader it is tested against - an encoder checked against anything other
# than the decoder that will consume it proves only that its author read the ABI
# twice the same way.
target_link_libraries(abi_syscall_test PRIVATE emnl::osabi emnl::oskernel emnl_warnings)
target_compile_options(abi_syscall_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME abi_syscall_test COMMAND abi_syscall_test)
set_tests_properties(abi_syscall_test PROPERTIES LABELS "m7-kernel")
