add_library(oskeys STATIC
    src/registry.cpp
    src/random_id_linux.cpp
)

add_library(emnl::oskeys ALIAS oskeys)

target_include_directories(oskeys
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(oskeys
    PUBLIC
        emnl::oscore
        emnl_warnings
)

target_compile_options(oskeys PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
