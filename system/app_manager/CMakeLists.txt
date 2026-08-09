add_library(emnl_app_manager STATIC
    src/manager_linux.cpp
    src/lifecycle.cpp
    src/storage_integration.cpp
    src/key_integration.cpp
    src/runtime_session.cpp
)

add_library(emnl::app_manager ALIAS emnl_app_manager)

target_include_directories(emnl_app_manager
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(emnl_app_manager
    PUBLIC
        emnl::osapp
        emnl::ospkg
        emnl::supervisor
        emnl::ossandbox
        emnl::storage_service
        emnl::key_service
        emnl_warnings
)

target_compile_options(emnl_app_manager PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
