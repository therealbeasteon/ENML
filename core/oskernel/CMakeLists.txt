add_library(oskernel STATIC
    src/abi.cpp
    src/rendezvous.cpp
)

add_library(emnl::oskernel ALIAS oskernel)

target_include_directories(oskernel
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(oskernel PUBLIC emnl::oscore emnl_warnings)

target_compile_options(oskernel PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
