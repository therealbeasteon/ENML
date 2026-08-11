add_executable(device_access_test access_test.cpp)
target_link_libraries(device_access_test PRIVATE emnl::osdevice emnl_warnings)
target_compile_options(device_access_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME device_access_test COMMAND device_access_test)
set_tests_properties(device_access_test PROPERTIES LABELS "m6-device")
