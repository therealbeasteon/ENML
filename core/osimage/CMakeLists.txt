add_library(osimage STATIC
    src/ckx.cpp
)

add_library(emnl::osimage ALIAS osimage)

target_include_directories(osimage
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(osimage PUBLIC emnl::oscore emnl_warnings)

target_compile_options(osimage PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-fno-exceptions -fno-rtti>
)
