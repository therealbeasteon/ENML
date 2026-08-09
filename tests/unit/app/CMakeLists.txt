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
add_test(NAME application_bootstrap_v2_test COMMAND application_bootstrap_v2_test)
set_tests_properties(application_bootstrap_v2_test PROPERTIES LABELS m2-broker)

add_executable(application_service_session_test service_session_test.cpp)
target_link_libraries(application_service_session_test PRIVATE emnl::osapp emnl_warnings)
target_compile_options(application_service_session_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME application_service_session_test COMMAND application_service_session_test)
set_tests_properties(application_service_session_test PROPERTIES LABELS m2-runtime)
