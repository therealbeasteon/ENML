add_executable(key_service_identity_test key_service_identity_test.cpp)
target_link_libraries(key_service_identity_test PRIVATE
    emnl::key_service
    emnl_warnings
)
target_compile_options(key_service_identity_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME key_service_identity_test COMMAND key_service_identity_test)
