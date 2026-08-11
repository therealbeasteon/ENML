add_executable(shell_task_model_test task_model_test.cpp)
target_link_libraries(shell_task_model_test PRIVATE osshell emnl_warnings)
target_compile_features(shell_task_model_test PRIVATE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(shell_task_model_test PRIVATE -fno-exceptions -fno-rtti)
endif()

add_test(NAME shell_task_model_test COMMAND shell_task_model_test)
set_tests_properties(shell_task_model_test PROPERTIES LABELS "m4-shell")

add_executable(shell_activation_test activation_test.cpp)
target_link_libraries(shell_activation_test PRIVATE osshell emnl_warnings)
target_compile_features(shell_activation_test PRIVATE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(shell_activation_test PRIVATE -fno-exceptions -fno-rtti)
endif()

add_test(NAME shell_activation_test COMMAND shell_activation_test)
set_tests_properties(shell_activation_test PROPERTIES LABELS "m4-shell")

add_executable(shell_preview_policy_test preview_policy_test.cpp)
target_link_libraries(shell_preview_policy_test PRIVATE osshell emnl_warnings)
target_compile_features(shell_preview_policy_test PRIVATE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(shell_preview_policy_test PRIVATE -fno-exceptions -fno-rtti)
endif()

add_test(NAME shell_preview_policy_test COMMAND shell_preview_policy_test)
set_tests_properties(shell_preview_policy_test PROPERTIES LABELS "m4-shell")

add_executable(shell_consent_test consent_test.cpp)
target_link_libraries(shell_consent_test PRIVATE osshell emnl_warnings)
target_compile_features(shell_consent_test PRIVATE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(shell_consent_test PRIVATE -fno-exceptions -fno-rtti)
endif()

add_test(NAME shell_consent_test COMMAND shell_consent_test)
set_tests_properties(shell_consent_test PROPERTIES LABELS "m4-shell")


add_executable(shell_unlock_test unlock_test.cpp)
target_link_libraries(shell_unlock_test PRIVATE osshell emnl_warnings)
target_compile_features(shell_unlock_test PRIVATE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(shell_unlock_test PRIVATE -fno-exceptions -fno-rtti)
endif()

add_test(NAME shell_unlock_test COMMAND shell_unlock_test)
set_tests_properties(shell_unlock_test PROPERTIES LABELS "m4-shell")
