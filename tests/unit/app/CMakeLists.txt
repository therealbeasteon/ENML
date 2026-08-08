add_executable(application_principal_store_test principal_store_test.cpp)
target_link_libraries(application_principal_store_test PRIVATE emnl::osapp emnl_warnings)
target_compile_options(application_principal_store_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME application_principal_store_test COMMAND application_principal_store_test)
