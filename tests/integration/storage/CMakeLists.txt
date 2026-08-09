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

add_executable(storage_supervised_service_test storage_supervised_service_test.cpp)
target_link_libraries(storage_supervised_service_test PRIVATE
    emnl::storage_service
    emnl::storage_generated
    emnl::supervisor
    emnl_warnings
)
target_compile_options(storage_supervised_service_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_dependencies(storage_supervised_service_test system_storage)
add_test(
    NAME storage_supervised_service_test
    COMMAND storage_supervised_service_test $<TARGET_FILE:system_storage>
)

add_executable(storage_service_restart_test storage_service_restart_test.cpp)
target_link_libraries(storage_service_restart_test PRIVATE
    emnl::storage_service
    emnl::storage_generated
    emnl::supervisor
    emnl_warnings
)
target_compile_options(storage_service_restart_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_dependencies(storage_service_restart_test system_storage)
add_test(
    NAME storage_service_restart_test
    COMMAND storage_service_restart_test $<TARGET_FILE:system_storage>
)
