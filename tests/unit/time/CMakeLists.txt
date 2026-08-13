add_executable(time_protection_test protection_test.cpp)
target_link_libraries(time_protection_test PRIVATE emnl::ostime emnl_warnings)
target_compile_options(time_protection_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME time_protection_test COMMAND time_protection_test)
set_tests_properties(time_protection_test PROPERTIES LABELS "m6-time")
