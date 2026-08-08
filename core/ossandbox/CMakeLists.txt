add_library(emnl_ossandbox STATIC
    src/sandbox_linux.cpp
)

add_library(emnl::ossandbox ALIAS emnl_ossandbox)

target_include_directories(emnl_ossandbox
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(emnl_ossandbox PUBLIC emnl::oscore emnl_warnings)

target_compile_options(emnl_ossandbox PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
