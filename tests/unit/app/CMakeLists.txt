add_executable(application_principal_store_test principal_store_test.cpp)
target_link_libraries(application_principal_store_test PRIVATE emnl::osapp emnl_warnings)
target_compile_options(application_principal_store_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME application_principal_store_test COMMAND application_principal_store_test)

add_executable(application_bootstrap_v2_test bootstrap_v2_test.cpp)
target_link_libraries(application_bootstrap_v2_test PRIVATE emnl::osapp emnl_warnings)
target_compile_options(application_bootstrap_v2_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
# M2-only CTest names deliberately avoid the legacy M1 `application_` regex.
# Their build-target names remain descriptive and the M2 workflow selects them
# by explicit m2-broker/m2-runtime labels.
add_test(NAME m2_bootstrap_v2_test COMMAND application_bootstrap_v2_test)
set_tests_properties(m2_bootstrap_v2_test PROPERTIES LABELS m2-broker)

add_executable(application_service_session_test service_session_test.cpp)
target_link_libraries(application_service_session_test PRIVATE emnl::osapp emnl_warnings)
target_compile_options(application_service_session_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME m2_service_session_test COMMAND application_service_session_test)
set_tests_properties(m2_service_session_test PROPERTIES LABELS "m2-runtime;m3-display")

add_executable(application_input_event_test input_event_test.cpp)
target_link_libraries(application_input_event_test PRIVATE emnl::osapp emnl_warnings)
target_compile_options(application_input_event_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME application_input_event_test COMMAND application_input_event_test)
set_tests_properties(application_input_event_test PROPERTIES LABELS m3-display)
