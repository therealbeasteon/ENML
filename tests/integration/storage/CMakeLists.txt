add_executable(storage_service_integration_test storage_service_integration_test.cpp)
target_link_libraries(storage_service_integration_test PRIVATE
    emnl::storage_service
    emnl::storage_generated
    emnl_warnings
)
target_compile_options(storage_service_integration_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME storage_service_integration_test COMMAND storage_service_integration_test)
