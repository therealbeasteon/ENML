function(add_emnl_storage_integration_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE
        emnl::storage_service
        emnl::storage_generated
        emnl_warnings
    )
    target_compile_options(${name} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_emnl_storage_integration_test(
    storage_service_integration_test
    storage_service_integration_test.cpp
)

add_emnl_storage_integration_test(
    storage_service_identity_test
    storage_service_identity_test.cpp
)
