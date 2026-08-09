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

add_executable(storage_app_manager_restart_test storage_app_manager_restart_test.cpp)
target_link_libraries(storage_app_manager_restart_test PRIVATE
    emnl::app_manager
    emnl_warnings
)
target_compile_options(storage_app_manager_restart_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(
    NAME storage_app_manager_restart_test
    COMMAND storage_app_manager_restart_test $<TARGET_FILE:system.storage>
)
