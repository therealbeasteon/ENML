add_executable(package_identity_test package_identity_test.cpp)
target_link_libraries(package_identity_test PRIVATE emnl::ospkg emnl_warnings)
target_compile_options(package_identity_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME package_identity_test COMMAND package_identity_test)

add_executable(package_registry_test package_registry_test.cpp)
target_link_libraries(package_registry_test PRIVATE emnl::ospkg emnl_warnings)
target_compile_options(package_registry_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME package_registry_test COMMAND package_registry_test)

add_executable(package_analyzer_test package_analyzer_test.cpp)
target_link_libraries(package_analyzer_test PRIVATE emnl::ospkg emnl_warnings)
target_compile_options(package_analyzer_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME package_analyzer_test COMMAND package_analyzer_test)

add_executable(package_persistence_test package_persistence_test.cpp)
target_link_libraries(package_persistence_test PRIVATE emnl::ospkg emnl_warnings)
target_compile_options(package_persistence_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME package_persistence_test COMMAND package_persistence_test)
