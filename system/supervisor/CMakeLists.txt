add_library(emnl_supervisor STATIC
    src/supervisor.cpp
)

add_library(emnl::supervisor ALIAS emnl_supervisor)

target_include_directories(emnl_supervisor
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(emnl_supervisor
    PUBLIC
        emnl::osservice
        emnl::ossandbox
        emnl_warnings
)

target_compile_options(emnl_supervisor PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)

add_executable(os-supervisor src/main.cpp)
target_link_libraries(os-supervisor PRIVATE emnl::supervisor emnl_warnings)
target_compile_options(os-supervisor PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
