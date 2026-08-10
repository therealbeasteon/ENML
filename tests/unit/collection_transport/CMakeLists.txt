add_executable(collection_transport_test transport_test.cpp)
target_link_libraries(collection_transport_test PRIVATE
    emnl::oscollection
    emnl_warnings
)
target_compile_options(collection_transport_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME collection_transport_test COMMAND collection_transport_test)
set_tests_properties(collection_transport_test PROPERTIES LABELS m3-ui)

add_executable(collection_session_test session_test.cpp)
target_link_libraries(collection_session_test PRIVATE
    emnl::oscollection
    emnl_warnings
)
target_compile_options(collection_session_test PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
add_test(NAME collection_session_test COMMAND collection_session_test)
set_tests_properties(collection_session_test PROPERTIES LABELS m3-ui)
