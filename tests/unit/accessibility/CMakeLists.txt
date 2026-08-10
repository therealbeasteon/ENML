add_executable(accessibility_transport_test transport_test.cpp)
target_link_libraries(accessibility_transport_test PRIVATE
    emnl::osaccessibility
    emnl_warnings
)
target_compile_options(accessibility_transport_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME accessibility_transport_test COMMAND accessibility_transport_test)
set_tests_properties(accessibility_transport_test PROPERTIES LABELS m3-ui)
