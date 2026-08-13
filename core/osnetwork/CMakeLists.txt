add_library(osnetwork STATIC
    src/policy.cpp
)

add_library(emnl::osnetwork ALIAS osnetwork)

target_include_directories(osnetwork
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(osnetwork PUBLIC emnl::oscore emnl_warnings)

target_compile_options(osnetwork PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
