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

add_executable(accessibility_session_channel_test session_channel_test.cpp)
target_link_libraries(accessibility_session_channel_test PRIVATE
    emnl::osaccessibility
    emnl_warnings
)
target_compile_options(accessibility_session_channel_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME accessibility_session_channel_test COMMAND accessibility_session_channel_test)
set_tests_properties(accessibility_session_channel_test PROPERTIES LABELS m3-ui)
