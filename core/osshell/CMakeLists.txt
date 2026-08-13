add_library(osshell STATIC
    src/task_model.cpp
    src/activation.cpp
    src/compositor_client.cpp
    src/preview_policy.cpp
    src/consent.cpp
    src/unlock.cpp
    src/credential_rule.cpp
    src/chrome.cpp
)

target_include_directories(osshell PUBLIC include)
# Union of both sides: osui arrived with the M4.2-M4.10 consent/unlock work,
# emnl::osipc with M4.1's chrome lease.
target_link_libraries(osshell PUBLIC osapp oscore ospkg osdisplay osui emnl::osipc emnl_warnings)
target_compile_features(osshell PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(osshell PRIVATE -fno-exceptions -fno-rtti)
endif()
