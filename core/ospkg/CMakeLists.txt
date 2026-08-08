add_library(ospkg STATIC
    src/package.cpp
    src/registry.cpp
)

add_library(emnl::ospkg ALIAS ospkg)

target_include_directories(ospkg
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(ospkg
    PUBLIC
        emnl::oscore
        emnl_warnings
)

target_compile_options(ospkg PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
