add_executable(shell_task_model_test task_model_test.cpp)
target_link_libraries(shell_task_model_test PRIVATE osshell emnl_warnings)
target_compile_features(shell_task_model_test PRIVATE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(shell_task_model_test PRIVATE -fno-exceptions -fno-rtti)
endif()

add_test(NAME shell_task_model_test COMMAND shell_task_model_test)
set_tests_properties(shell_task_model_test PROPERTIES LABELS "m4-shell")
