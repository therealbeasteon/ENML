add_library(osshell STATIC
    src/task_model.cpp
    src/activation.cpp
    src/preview_policy.cpp
)

target_include_directories(osshell PUBLIC include)
target_link_libraries(osshell PUBLIC osapp oscore ospkg osdisplay emnl_warnings)
target_compile_features(osshell PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(osshell PRIVATE -fno-exceptions -fno-rtti)
endif()
